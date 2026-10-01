#!/bin/sh
# Run only inside the disposable Ubuntu 24.04 experiment VMs.
set -eu
. /etc/os-release
[ "$ID" = ubuntu ] && [ "$VERSION_ID" = 24.04 ] || {
    echo 'This provisioning script requires Ubuntu 24.04' >&2
    exit 1
}
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates curl gnupg
# Ubuntu 24.04 omits CRIU. Use the CRIU team's signed PPA, scoped to this source.
key=$(mktemp)
trap 'rm -f "$key"' EXIT
curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x4E2A48715C45AEEC077B48169B29EEC9246B6CE2' -o "$key"
fingerprint=$(gpg --batch --show-keys --with-colons "$key" | awk -F: '$1 == "fpr" {print $10; exit}')
[ "$fingerprint" = 4E2A48715C45AEEC077B48169B29EEC9246B6CE2 ] || exit 1
gpg --batch --yes --dearmor -o /usr/share/keyrings/telefork-criu.gpg "$key"
printf '%s\n' 'deb [signed-by=/usr/share/keyrings/telefork-criu.gpg] https://ppa.launchpadcontent.net/criu/ppa/ubuntu noble main' > /etc/apt/sources.list.d/telefork-criu.list
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends criu gcc libc6-dev gdb make
