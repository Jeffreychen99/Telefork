#!/bin/sh
# Private Lima runtime and two dedicated VMs; no Homebrew/system installation.
set -eu
cd "$(dirname "$0")/.."
export LIMA_HOME="$PWD/.lab/lima"
lima="$PWD/.lab/runtime/bin/limactl"
action=${1:-run}
if [ "$#" -gt 0 ]; then shift; fi
case "$action" in
    stop|status)
        [ -x "$lima" ] || { echo 'Lab runtime is not installed' >&2; exit 1; }
        if [ "$action" = status ]; then exec "$lima" list; fi
        "$lima" stop telefork-source
        "$lima" stop telefork-target
        exit 0
        ;;
    run|start) ;;
    *) echo 'Usage: sh tools/mac_lab.sh [run|start|stop|status] [experiment options]' >&2; exit 2 ;;
esac
[ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] || {
    echo 'This lab setup requires an Apple Silicon Mac' >&2
    exit 1
}
if [ ! -x "$lima" ]; then
    mkdir -p .lab/runtime
    curl -fL --retry 2 https://github.com/lima-vm/lima/releases/download/v2.2.0/lima-2.2.0-Darwin-arm64.tar.gz -o .lab/lima.tar.gz
    printf '%s\n' 'bbdef91774885a0d05f7b048c4eb89ae2bcf3a0c252ae7ca7934e63df76d93c3  .lab/lima.tar.gz' | shasum -a 256 -c -
    tar -xzf .lab/lima.tar.gz -C .lab/runtime
fi
for vm in telefork-source telefork-target; do
    if [ -f "$LIMA_HOME/$vm/lima.yaml" ]; then
        "$lima" start --tty=false "$vm"
    else
        "$lima" start --name="$vm" --vm-type=vz --cpus=1 --memory=1 --disk=8 --mount-none --containerd=none --tty=false template:ubuntu-24.04
    fi
done
if [ "$action" = run ]; then
    exec python3 tools/mac_experiment.py "$@"
fi
