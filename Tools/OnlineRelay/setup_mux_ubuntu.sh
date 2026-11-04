#!/usr/bin/env bash
# Adds the relay's one-port UDP entry (UDP 443 -> --mux-port 7443) to a relay VM already set up with setup_ubuntu.sh
# and setup_wss_ubuntu.sh. For players on a network that lets only well-known ports out (a school's, a shop's or an
# office's Wi-Fi): their game reaches the directory and every room over UDP 443 instead of falling back to the
# WebSocket (TCP), where every lost packet holds up all that follow it.
#
# Run it after update_relay_ssh.sh has put the new ci_relay.py there (it knows --mux-port):
#     ssh -i ~/.ssh/oracle_ci.key ubuntu@<VM address> 'sudo bash -s' < setup_mux_ubuntu.sh
# and add an ingress rule to the VCN's security list in the Oracle console: source 0.0.0.0/0, UDP, destination port 443.
# Safe to run again. Undo: remove /etc/systemd/system/ci-relay.service.d/zz-mux.conf (daemon-reload, restart), and
# the two iptables rules below (iptables -D ... the same way, then netfilter-persistent save).
set -euo pipefail
mux_port=7443
public_port=443

grep -q -- "--mux-port" /opt/ci-relay/ci_relay.py || { echo "先に update_relay_ssh.sh で新しい ci_relay.py を送ってください"; exit 1; }

# The service: the same command as the WSS set-up's, with the one-port entry (the relay runs unprivileged: it listens
# on 7443 and the kernel redirects 443 there).
mkdir -p /etc/systemd/system/ci-relay.service.d
cat > /etc/systemd/system/ci-relay.service.d/zz-mux.conf <<EOF
[Service]
ExecStart=
ExecStart=/opt/ci-relay/venv/bin/python -u /opt/ci-relay/ci_relay.py --bind 0.0.0.0 --port 7700 --room-ports 7701-7760 --data /var/lib/ci-relay/ranks.json --ws-bind 127.0.0.1 --ws-port 7780 --mux-port ${mux_port}
EOF

# Let the redirected datagrams in (before Oracle's REJECT rule), and send UDP 443 to the entry.
if ! iptables -C INPUT -p udp --dport ${mux_port} -j ACCEPT 2>/dev/null; then
    reject_line="$(iptables -L INPUT --line-numbers -n | awk '$2 == "REJECT" {print $1; exit}')"
    if [ -n "$reject_line" ]; then
        iptables -I INPUT "$reject_line" -p udp --dport ${mux_port} -j ACCEPT
    else
        iptables -A INPUT -p udp --dport ${mux_port} -j ACCEPT
    fi
fi
iptables -t nat -C PREROUTING -p udp --dport ${public_port} -j REDIRECT --to-ports ${mux_port} 2>/dev/null \
    || iptables -t nat -A PREROUTING -p udp --dport ${public_port} -j REDIRECT --to-ports ${mux_port}
netfilter-persistent save >/dev/null 2>&1 || echo "（iptables の保存に失敗：iptables-persistent を確認してください）"

systemctl daemon-reload
systemctl restart ci-relay
sleep 2
echo "== ci-relay: $(systemctl is-active ci-relay)"
journalctl -u ci-relay --no-pager -n 4 | cut -c1-160
ss -ulpn | grep -E ":(7700|${mux_port}) " || true
echo "== Oracle のセキュリティ・リストに UDP ${public_port} の受信ルールがあることも確認してください"
