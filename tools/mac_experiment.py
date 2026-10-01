#!/usr/bin/env python3
"""Run Telefork across two private ARM64 Linux VMs on an Apple Silicon Mac.

Usage: python3 tools/mac_experiment.py [--skip-install] [--stop]
Lima must be installed at .lab/runtime/bin/limactl (see docs/mac-experiment.md).
VMs remain available for inspection unless --stop is supplied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
LAB = ROOT / '.lab'
LIMA = LAB / 'runtime/bin/limactl'
SOURCE = 'telefork-source'
TARGET = 'telefork-target'
HELPER = '/opt/telefork/tools/telefork.py'
MARKER = 424200000
ENV = {**os.environ, 'LIMA_HOME': str(LAB / 'lima')}


def lima(*args, input=None, timeout=600):
    result = subprocess.run([str(LIMA), *map(str, args)], input=input,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=ENV, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"limactl {' '.join(map(str, args))}:\n"
                           + result.stderr.decode(errors='replace')
                           + result.stdout.decode(errors='replace'))
    return result.stdout


def guest(vm, *args, **kwargs):
    return lima('shell', '--workdir', '/tmp', vm, 'sudo', *args, **kwargs)


def stage(vm, name, content):
    guest(vm, 'tee', name, input=content)


def observe(vm, pid, value=None):
    commands = []
    if value is not None:
        commands += ['-ex', f'set variable *counter = {int(value)}']
    commands += ['-ex', 'printf "TELEFORK_COUNTER=%lu\\n", *counter', '-ex', 'detach']
    output = guest(vm, 'gdb', '-q', '-nx', '-nh', '-batch', '-p', str(pid), *commands).decode()
    match = re.search(r'TELEFORK_COUNTER=(\d+)', output)
    if not match:
        raise RuntimeError(f'No counter value from {vm}: {output}')
    return int(match.group(1))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-install', action='store_true', help='VM packages already installed')
    parser.add_argument('--stop', action='store_true', help='Stop both lab VMs after the experiment')
    args = parser.parse_args()
    if not LIMA.is_file():
        parser.error('Install the private Lima runtime first; see docs/mac-experiment.md')
    run_id = time.strftime('%Y%m%d-%H%M%S')
    out = LAB / 'results' / run_id
    out.mkdir(parents=True, mode=0o700)
    remote_images = '/var/tmp/telefork-' + run_id
    report = {'passed': False, 'transport': 'SSH via Mac (dump/restore subcommands)',
              'source': SOURCE, 'target': TARGET, 'counter_marker': MARKER,
              'remote_images': remote_images}
    pids = {}
    try:
        for vm in (SOURCE, TARGET):
            print(f'Checking {vm}...', flush=True)
            if not args.skip_install:
                print(f'Installing guest packages in {vm}...', flush=True)
                log = guest(vm, 'sh', '-s', input=(ROOT / 'tools/provision-lab.sh').read_bytes(), timeout=900)
                (out / f'{vm}-install.log').write_bytes(log)
            boot_id = guest(vm, 'cat', '/proc/sys/kernel/random/boot_id').decode().strip()
            kernel = guest(vm, 'uname', '-r').decode().strip()
            report[vm] = {'boot_id': boot_id, 'kernel': kernel}
            guest(vm, 'mkdir', '-p', '/opt/telefork/tools', '/opt/telefork/build')
            stage(vm, HELPER, (ROOT / 'tools/telefork.py').read_bytes())
            report[vm]['compatibility'] = json.loads(guest(vm, 'python3', HELPER, 'check'))
        require(report[SOURCE]['boot_id'] != report[TARGET]['boot_id'], 'VMs must have distinct kernels/boot IDs')
        require(report[SOURCE]['compatibility'] == report[TARGET]['compatibility'], 'VM compatibility metadata differs')

        print('Building one binary and copying it unchanged to the destination...', flush=True)
        stage(SOURCE, '/opt/telefork/counter.c', (ROOT / 'examples/counter.c').read_bytes())
        guest(SOURCE, 'gcc', '-O0', '-g', '-Wall', '-Wextra', '-Werror', '-std=c11',
              '/opt/telefork/counter.c', '-o', '/opt/telefork/build/counter')
        binary_archive = guest(SOURCE, 'tar', '-C', '/opt/telefork', '-cf', '-', 'build/counter')
        guest(TARGET, 'tar', '-C', '/opt/telefork', '-xf', '-', input=binary_archive)
        hashes = [guest(vm, 'sha256sum', '/opt/telefork/build/counter').decode().split()[0]
                  for vm in (SOURCE, TARGET)]
        require(hashes[0] == hashes[1], 'Demo binaries differ')
        report['binary_sha256'] = hashes[0]

        # Reserve a high PID in this disposable VM to avoid target system PIDs.
        # No PID namespace/cgroup/container state needs to be migrated.
        launch = '''import os, subprocess
from pathlib import Path
Path('/proc/sys/kernel/ns_last_pid').write_text('40000')
p = subprocess.Popen(['/opt/telefork/build/counter'], cwd='/opt/telefork',
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    start_new_session=True)
print(p.pid)
'''
        pids[SOURCE] = int(guest(SOURCE, 'python3', '-c', launch))
        time.sleep(1)
        report['source_pid'] = pids[SOURCE]
        report['source_before_dump'] = observe(SOURCE, pids[SOURCE], MARKER)
        require(report['source_before_dump'] == MARKER, 'Could not seed heap state')
        print(f'Source PID {pids[SOURCE]} heap counter seeded to {MARKER}; checkpointing...', flush=True)
        guest(SOURCE, 'python3', HELPER, 'dump', str(pids[SOURCE]), remote_images)
        archive = guest(SOURCE, 'tar', '-C', remote_images, '-cf', '-', '.')
        (out / 'checkpoint.tar').write_bytes(archive)
        report['checkpoint_bytes'] = len(archive)
        report['checkpoint_sha256'] = hashlib.sha256(archive).hexdigest()
        guest(TARGET, 'mkdir', '-m', '700', remote_images)
        guest(TARGET, 'tar', '-C', remote_images, '-xf', '-', input=archive)
        print('Restoring on the second Linux kernel...', flush=True)
        restored = json.loads(guest(TARGET, 'python3', HELPER, 'restore', remote_images))
        pids[TARGET] = int(restored['pid'])
        report['target_pid'] = pids[TARGET]
        report['target_after_restore'] = observe(TARGET, pids[TARGET])
        require(MARKER <= report['target_after_restore'] < MARKER + 600,
                'Destination did not resume the seeded heap state')
        report['source_after_restore'] = observe(SOURCE, pids[SOURCE])
        time.sleep(3)
        report['source_later'] = observe(SOURCE, pids[SOURCE])
        report['target_later'] = observe(TARGET, pids[TARGET])
        require(report['source_later'] > report['source_after_restore'], 'Source stopped advancing')
        require(report['target_later'] > report['target_after_restore'], 'Destination stopped advancing')
        branch_marker = MARKER + 1000000
        report['target_branch_value'] = observe(TARGET, pids[TARGET], branch_marker)
        report['source_after_target_mutation'] = observe(SOURCE, pids[SOURCE])
        require(report['target_branch_value'] == branch_marker, 'Destination heap mutation failed')
        require(MARKER <= report['source_after_target_mutation'] < branch_marker,
                'The two heaps are not independent')
        report['passed'] = True
        print(json.dumps(report, indent=2), flush=True)
    except Exception as exc:
        report['error'] = str(exc)
        print(str(exc), file=sys.stderr, flush=True)
    finally:
        for vm in (SOURCE, TARGET):
            for filename in ('dump.log', 'restore.log'):
                try:
                    (out / f'{vm}-{filename}').write_bytes(guest(vm, 'cat', remote_images + '/' + filename, timeout=15))
                except Exception:
                    pass
        # Only terminate experiment PIDs, checking their executable first.
        for vm, pid in pids.items():
            try:
                cleanup = 'import os,signal,sys; p=int(sys.argv[1]); exe=os.readlink(f"/proc/{p}/exe"); os.kill(p,signal.SIGTERM) if exe == "/opt/telefork/build/counter" else None'
                guest(vm, 'python3', '-c', cleanup, str(pid), timeout=15)
            except Exception as exc:
                report.setdefault('cleanup_errors', []).append(str(exc))
        if args.stop:
            for vm in (SOURCE, TARGET):
                try:
                    lima('stop', vm, timeout=120)
                except Exception as exc:
                    report.setdefault('cleanup_errors', []).append(str(exc))
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f'Evidence saved to {out}', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
