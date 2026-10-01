# Local Mac experiment

The experiment uses **two independent ARM64 Ubuntu 24.04 Linux VMs** on an
Apple Silicon Mac, driven by Lima and Apple's Virtualization.framework. Each
VM has one virtual CPU, 1 GiB of RAM and an 8 GiB sparse disk. No host directories
are shared into the guests. This tests Linux-to-Linux restoration, not migration
of a native macOS process.

Run from the Telefork repository:

```sh
sh tools/mac_lab.sh run --stop
```

The first run downloads the pinned, checksum-verified official Lima 2.2.0
runtime and an Ubuntu cloud image, boots both VMs, and provisions CRIU and build
and debugging tools. Allow several minutes and several GB of disk space.
Ubuntu 24.04 has no CRIU package in its standard repositories; the guest-only
provisioner uses the CRIU team's PPA with its verified signing-key fingerprint
and a repository-specific `signed-by` keyring. It does not install software on
the macOS system. Lima's downloaded image cache lives in `~/Library/Caches/lima`;
the runtime, VM disks and private evidence live in the gitignored `.lab/`.

After provisioning, repeat faster with:

```sh
sh tools/mac_lab.sh run --skip-install --stop
```

Omit `--stop` to leave the VMs up for inspection. The runner terminates the two
demo processes after recording the result. Commands to manage just this lab:

```sh
sh tools/mac_lab.sh status
sh tools/mac_lab.sh start
sh tools/mac_lab.sh stop
```

## What constitutes success

The runner builds the C demo once, transfers the identical executable to the
second VM, and records matching executable SHA-256 hashes and distinct Linux
boot IDs. It uses GDB to set the live source's heap counter to **424,200,000**,
a value the demo never initializes itself to. Then it:

1. Invokes Telefork's `dump` subcommand with the source left running.
2. Transfers the completed checkpoint over SSH through the Mac.
3. Invokes Telefork's `restore` subcommand in the other VM.
4. Reads the destination heap and checks the distinctive value survived.
5. Verifies both source and destination counters keep increasing.
6. Changes only the destination counter to **425,200,000** and checks the source
   is unaffected, establishing independent process memory.

This exercises real CRIU checkpoint/restore, not mocks. The Mac relays the
checkpoint because Lima's default private guest networks are isolated. It tests
the separate `dump` and `restore` commands, not the `clone` command's direct
source-to-destination SSH setup. A successful result proves this workload on this
VM configuration; it does not establish arbitrary application portability.

## Observed result: October 1, 2026

The experiment passed twice on this Mac with Ubuntu kernel 6.8.0-134-generic
and CRIU 4.2.1. Both guests were aarch64 with 4096-byte pages and different
boot IDs. The checkpoint archive was 163,840 bytes. First-run observations:

| Measurement | Source | Destination |
| --- | ---: | ---: |
| Seeded counter / first restored reading | 424,200,000 | 424,200,000 |
| Later reading | 424,200,004 | 424,200,004 |
| After changing only destination memory | 424,200,004 | 425,200,000 |

Both processes used PID 40001 in their respective VMs. The destination was
created by CRIU restore; the runner never starts the demo executable there.

## Evidence and cleanup

Each run saves `.lab/results/TIMESTAMP/report.json`, CRIU logs, and a private
checkpoint archive. `passed: true` requires every state and independence check
to pass. On failure, the report contains the command/error and available CRIU
logs. Reports include the kernel/CRIU versions for reproducibility. Source PID
allocation is advanced to 40000 inside the disposable VM to avoid a collision
with the target's system processes.

Checkpoint archives contain memory; `.lab/results` should remain private and
must not be committed. Stopping VMs releases RAM but keeps their disks and
results so the experiment can be repeated.

References: [Lima installation](https://lima-vm.io/docs/installation/),
[Lima's VZ driver](https://lima-vm.io/docs/config/vmtype/vz/),
[CRIU team's PPA](https://launchpad.net/~criu/+archive/ubuntu/ppa).
