# Telefork

Clone a running **Linux process tree onto another compatible Linux machine or
VM**, leaving the original running. The new command-line implementation uses
CRIU for checkpoint/restore and SSH/SCP for transport. It is not an in-process
`fork()` API: the destination resumes saved execution state with the original
PIDs, and neither process receives a special parent/child return value.

The original C/Mach experiments in `src/` and `thread_hijack.c` are preserved,
including existing local changes. They are **not a working migration
implementation**; do not run the old checked-in binaries. Their blockers include
missing memory transfer, an exiting snapshot child, unlinked register lists,
out-of-bounds register transmission, partial socket I/O, and unsafe register
restoration inside pthreads. Fixing transport alone cannot make them work.

## Run the experiment on an Apple Silicon Mac

```sh
sh tools/mac_lab.sh run --stop
```

This provisions two small, separate ARM64 Linux VMs, seeds a distinctive value
in the C demo's heap, transfers its checkpoint over SSH through the Mac, restores
it, and verifies both copies advance independently. The first run downloads
Lima/Ubuntu and installs packages inside the guests. `--stop` shuts down the VMs
afterward. See [the Mac experiment guide](docs/mac-experiment.md) for details.

**Verified on October 1, 2026:** two Ubuntu 24.04 ARM64 VMs with kernel
6.8.0-134-generic and CRIU 4.2.1 successfully restored the exact seeded heap
value, continued executing, and passed the independent-memory check. This
validates the `dump`/`restore` path with an SSH relay, not the direct `clone`
command's SSH setup or arbitrary applications.

## Requirements

Start with two disposable Linux VMs with the same CPU architecture/features,
kernel, distribution, CRIU version, and identical executables/libraries at
identical absolute paths. macOS processes cannot be restored into Linux; on
Apple Silicon, run both applications inside compatible ARM64 Linux VMs.

Install Python 3.9+, CRIU, OpenSSH, a C compiler, make, and GDB (for verification).
Place this repository at `/opt/telefork` on **both** VMs. Run
`sudo python3 /opt/telefork/tools/telefork.py check` on each VM; resolve kernel
capability failures before continuing. The helper requires root. Automatic
cloning needs noninteractive SSH/SCP from the invoking root account and
noninteractive `sudo` on the target; use separate stages below otherwise.
Host keys must already be trusted. Use SSH aliases for custom ports/IPv6.

Images contain process memory and potentially credentials: keep them private.
Restore only trusted images, since restoration executes their code as root.
The tool creates private image directories and retains logs/images for diagnosis.

## Two-VM verification

On the source, build the demo and copy the **same binary** to the same path on
the destination. Do not rebuild independently. Arrange write access to the target
repository for the account used by SCP first.

```sh
cd /opt/telefork
make
ssh user@target 'mkdir -p /opt/telefork/build'
scp build/counter user@target:/opt/telefork/build/counter
./build/counter &
pid=$!
sleep 5
sudo gdb -q -batch -p "$pid" -ex 'p *counter' -ex detach
sudo python3 tools/telefork.py clone "$pid" user@target --images /var/tmp/telefork-demo
```

Choose a **new** images directory for each attempt. The demo increments a heap
counter starting at 1,000,000 once per second and opens `/dev/null` for its
standard streams, avoiding dependence on a terminal or mutable data files.
The clone command prints the restored PID as JSON only after CRIU succeeds.
On the destination:

```sh
sudo gdb -q -batch -p RESTORED_PID -ex 'p *counter' -ex detach
```

Compare the value with the source reading taken before checkpointing. It should
be at least that large and keep increasing. Verify that the source also remains
alive and increasing. This distinguishes resumed memory from a freshly launched
program. Stop both demo processes with `kill` when finished.

CRIU restores original PIDs: if one is in use, restore can fail. Use a fresh
target VM or a dedicated PID namespace; this helper does not orchestrate
namespaces. Check CRIU logs if restore fails even on matching machines.

For interactive SSH/sudo authentication, run separate stages:

```sh
# Source
sudo python3 tools/telefork.py dump "$pid" /var/tmp/telefork-demo
# Transfer using an account that can read the private directory:
sudo scp -r /var/tmp/telefork-demo user@target:/var/tmp/telefork-demo
# Destination
sudo python3 /opt/telefork/tools/telefork.py restore /var/tmp/telefork-demo
```

## Boundaries and failures

This is checkpoint-and-clone, not transparent migration or a filesystem snapshot.
The source resumes after the dump even if transfer/restore fails. CRIU handles
memory, registers, and supported kernel resources; SSH moves only checkpoint
images. Separately provide matching files, working directories, mounts,
identities, and dependencies. CPU architecture/page size/CRIU version are
checked; CPU features, kernel, and filesystem compatibility still depend on the
VM configuration and CRIU checks.

Start with isolated CPU computations. Established network connections, GPU state,
devices, external IPC, and mutable open files are outside this demo's supported
scope. TCP restoration and file validation bypasses are not enabled. Do not
clone processes writing shared data or performing non-idempotent external
operations without application coordination. Clones also inherit identical RNG
state until explicitly reseeded.

On failure, inspect `dump.log` locally or `restore.log` in the printed destination
directory. Failed dumps have no completion manifest; existing checkpoints are
never overwritten. Inspect the destination for leftover tasks after a failed
restore; the helper does not guarantee transactional rollback or automatic retry.
Remove private checkpoint directories manually after they are no longer needed.

## Development and applications

`make test` compiles the C demo with warnings as errors and runs unit tests for
compatibility, private directories, invalid SSH input, and dump/transfer/restore
failure handling. Unit tests mock CRIU. The Mac lab and the manual two-VM
procedure exercise actual Linux restoration; compiling the demo on macOS alone
does not.

Useful applications:

- Branch a warmed-up simulation into separate experiments.
- Clone CPU workers after expensive initialization.
- Preserve an initialized process for debugging and reproducibility.
- Checkpoint long batch computations for later restart on another machine.

Branches need an application mechanism to receive different work, or they repeat
the same computation. Durable recovery also needs consistent filesystem
snapshots and storage. Single-owner migration needs coordination to retire the
source only after destination verification; this tool always leaves it running.

References: [CRIU live migration](https://criu.org/Live_migration),
[leave-running caveats](https://criu.org/Advanced_usage), and the original
[Rust Telefork inspiration](https://github.com/trishume/telefork).
