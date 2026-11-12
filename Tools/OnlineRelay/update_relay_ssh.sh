#!/usr/bin/env bash
# Puts this folder's ci_relay.py, ci_relay_ws.py and ci_gallery.py on a relay VM over SSH (Oracle Cloud and the like, set up with
# setup_ubuntu.sh / setup_wss_ubuntu.sh), keeps the old ones in /opt/ci-relay/backup-<time>/, and restarts it:
#     bash update_relay_ssh.sh ubuntu@<VM address> [SSH key, default ~/.ssh/oracle_ci.key]
# Rooms being played are cut off by the restart. Works from Git Bash on Windows (CRLF is turned into LF on the way).
set -euo pipefail
target="${1:?使い方: bash update_relay_ssh.sh ubuntu@<VMのアドレス> [SSH鍵]}"
key="${2:-$HOME/.ssh/oracle_ci.key}"
here="$(cd "$(dirname "$0")" && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
for file in ci_relay.py ci_relay_ws.py ci_gallery.py; do
    tr -d '\r' < "$here/$file" > "$stage/$file"
done
stamp="$(date +%Y%m%d-%H%M%S)"
echo "== $target に送ります"
tar -czf - -C "$stage" ci_relay.py ci_relay_ws.py ci_gallery.py | ssh -i "$key" -o BatchMode=yes "$target" "
    set -e
    d=/opt/ci-relay
    t=\$(mktemp -d)
    tar -xzf - -C \$t
    python3 -m py_compile \$t/ci_relay.py \$t/ci_relay_ws.py \$t/ci_gallery.py
    sudo mkdir -p \$d/backup-$stamp
    sudo cp \$d/ci_relay.py \$d/ci_relay_ws.py \$d/ci_gallery.py \$d/backup-$stamp/ 2>/dev/null || true
    sudo install -o root -g root -m 644 \$t/ci_relay.py \$t/ci_relay_ws.py \$t/ci_gallery.py \$d/
    rm -rf \$t
    sudo systemctl restart ci-relay
    sleep 2
    echo \"== ci-relay: \$(systemctl is-active ci-relay)  (前のファイル: \$d/backup-$stamp)\"
    sudo journalctl -u ci-relay --no-pager -n 3 | cut -c1-160
"
