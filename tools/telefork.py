#!/usr/bin/env python3
"""Clone a Linux process tree with CRIU. Requires Python 3.9+ and root."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import uuid


def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def check():
    if platform.system() != "Linux":
        raise ValueError("Checkpoint/restore requires Linux; use a Linux VM on macOS.")
    if os.geteuid() != 0:
        raise ValueError("Run as root (sudo); CRIU needs privileged kernel access.")
    if not shutil.which("criu"):
        raise ValueError("Install CRIU first.")
    run(["criu", "check"], stdout=sys.stderr)
    version = run(["criu", "--version"], capture_output=True, text=True).stdout.strip()
    return {"format": 1, "machine": platform.machine(),
            "page_size": os.sysconf("SC_PAGE_SIZE"), "criu": version}


def compatible(source, target):
    if not isinstance(source, dict) or not isinstance(target, dict):
        raise ValueError("Invalid checkpoint compatibility metadata")
    for key in ("format", "machine", "page_size", "criu"):
        if key not in source or key not in target or source[key] != target[key]:
            raise ValueError(f"Incompatible {key}: {source.get(key)!r} vs {target.get(key)!r}")


def dump(pid, directory, info):
    if pid <= 1 or pid == os.getpid():
        raise ValueError("Choose an application PID greater than 1, not Telefork itself.")
    os.kill(pid, 0)
    directory = Path(directory).resolve()
    directory.mkdir(mode=0o700)  # Never overwrite/reuse a checkpoint.
    run(["criu", "dump", "--tree", pid, "--images-dir", directory,
         "--leave-running", "--shell-job", "--log-file", "dump.log", "-v4"])
    # Written only after a complete dump; failed dumps cannot be restored by this tool.
    (directory / "telefork.json").write_text(json.dumps(info) + "\n")
    return directory


def restore(directory, info):
    directory = Path(directory).resolve()
    compatible(json.loads((directory / "telefork.json").read_text()), info)
    # CRIU restores original PIDs. Use a separate VM with no conflicting PIDs.
    run(["criu", "restore", "--images-dir", directory, "--shell-job",
         "--restore-detached", "--pidfile", str(directory / "restored.pid"),
         "--log-file", "restore.log", "-v4"], stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL)
    return int((directory / "restored.pid").read_text().strip())


def destination(value):
    # A deliberately restricted SSH alias/hostname, optionally prefixed by user@.
    if not re.fullmatch(r"(?:[A-Za-z0-9_][A-Za-z0-9_.-]*@)?[A-Za-z0-9][A-Za-z0-9_.-]*", value):
        raise argparse.ArgumentTypeError("Use an SSH hostname/alias, optionally user@host.")
    return value


def remote(host, helper, *args):
    command = shlex.join(["sudo", "-n", "python3", helper, *map(str, args)])
    return run(["ssh", "-oBatchMode=yes", "--", host, command],
               capture_output=True, text=True)


def clone(args, info):
    # Check destination before snapshotting or transferring anything.
    target = json.loads(remote(args.host, args.remote_helper, "check").stdout)
    compatible(info, target)
    directory = dump(args.pid, args.images, info)
    remote_dir = "/var/tmp/telefork-" + uuid.uuid4().hex
    run(["ssh", "-oBatchMode=yes", "--", args.host,
         shlex.join(["mkdir", "-m", "700", remote_dir])])
    print(f"Checkpoint: {directory}; destination: {args.host}:{remote_dir}", file=sys.stderr)
    # Copy contents into the pre-created private directory. No shell/glob expansion.
    files = sorted(str(p) for p in directory.iterdir())
    run(["scp", "-q", "-oBatchMode=yes", "--", *files, f"{args.host}:{remote_dir}/"])
    result = remote(args.host, args.remote_helper, "restore", remote_dir)
    print(result.stdout, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check", help="Check Linux kernel/CRIU and print compatibility metadata")
    d = sub.add_parser("dump", help="Checkpoint while leaving the source running")
    d.add_argument("pid", type=int)
    d.add_argument("images", help="New checkpoint directory (must not exist)")
    r = sub.add_parser("restore", help="Restore a trusted checkpoint on a compatible VM")
    r.add_argument("images")
    c = sub.add_parser("clone", help="Checkpoint, copy over SSH, restore on another VM")
    c.add_argument("pid", type=int)
    c.add_argument("host", type=destination)
    c.add_argument("--images", required=True, help="New local checkpoint directory")
    c.add_argument("--remote-helper", default="/opt/telefork/tools/telefork.py")
    args = parser.parse_args()
    try:
        info = check()
        if args.command == "check":
            print(json.dumps(info))
        elif args.command == "dump":
            print(dump(args.pid, args.images, info))
        elif args.command == "restore":
            print(json.dumps({"pid": restore(args.images, info)}))
        else:
            clone(args, info)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"telefork: {exc}", file=sys.stderr)
        if isinstance(exc, subprocess.CalledProcessError) and exc.stderr:
            print(exc.stderr, file=sys.stderr)
        print("Check dump.log/restore.log in the image directory. Checkpoints are retained.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
