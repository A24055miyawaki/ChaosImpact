#!/usr/bin/env bash
# Sets up the Chaos Impact relay on an Ubuntu VM (Oracle Cloud's Ubuntu images; Arm or x86).
# Run from the folder holding ci_relay.py and ci-relay.service:   sudo bash setup_ubuntu.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "run with sudo" >&2
    exit 1
fi
here="$(cd "$(dirname "$0")" && pwd)"

# Python 3 (Ubuntu images have it already).
command -v python3 >/dev/null || { apt-get update && apt-get install -y python3; }

# A user of its own, with no login.
id cirelay >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin cirelay
install -d -o root -g root -m 755 /opt/ci-relay
install -o root -g root -m 644 "$here/ci_relay.py" /opt/ci-relay/ci_relay.py
install -o root -g root -m 644 "$here/ci-relay.service" /etc/systemd/system/ci-relay.service

# Oracle's Ubuntu images block incoming traffic in iptables besides the VCN's security list: open the relay's
# UDP ports here too, and keep the rule across reboots.
if ! iptables -C INPUT -p udp --dport 7700:7760 -j ACCEPT 2>/dev/null; then
    # Ahead of the image's catch-all REJECT rule (or at the end when there is none).
    reject_line="$(iptables -L INPUT --line-numbers -n | awk '$2 == "REJECT" {print $1; exit}')"
    if [ -n "$reject_line" ]; then
        iptables -I INPUT "$reject_line" -p udp --dport 7700:7760 -j ACCEPT
    else
        iptables -A INPUT -p udp --dport 7700:7760 -j ACCEPT
    fi
fi
if command -v netfilter-persistent >/dev/null; then
    netfilter-persistent save
else
    DEBIAN_FRONTEND=noninteractive apt-get install -y iptables-persistent
    netfilter-persistent save
fi

systemctl daemon-reload
systemctl enable --now ci-relay
# (Run again for an update: the new ci_relay.py starts.)
systemctl restart ci-relay
sleep 1
systemctl --no-pager status ci-relay | head -n 12
echo
echo "Relay running. Public address for the game: <this VM's public IP>:7700"
echo "Logs: journalctl -u ci-relay -f"
