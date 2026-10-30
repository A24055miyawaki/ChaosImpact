#!/usr/bin/env bash
# Add WSS/TCP443 to the existing relay. Keeps UDP and /var/lib/ci-relay/ranks.json.
# Run as root next to ci_relay.py, ci_relay_ws.py and requirements-wss.txt:
#   sudo bash setup_wss_ubuntu.sh 132.145.120.172
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo 'Run with sudo.' >&2; exit 1; }
ip="${1:?Pass the VM public IPv4 address}"
python3 -c 'import ipaddress,sys; ipaddress.IPv4Address(sys.argv[1])' "$ip"
name="${ip//./-}.sslip.io"
here="$(cd -- "$(dirname -- "$0")" && pwd)"
for file in ci_relay.py ci_relay_ws.py requirements-wss.txt; do
    [ -f "$here/$file" ] || { echo "Missing $file" >&2; exit 1; }
done
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq python3-venv nginx ca-certificates
backup="/opt/ci-relay/backup-$(date +%Y%m%d-%H%M%S)"
install -d -m 755 "$backup" /opt/ci-relay /var/www/ci-relay-acme
for file in ci_relay.py ci_relay_ws.py; do
    [ ! -f "/opt/ci-relay/$file" ] || cp -p "/opt/ci-relay/$file" "$backup/"
    install -m 644 "$here/$file" "/opt/ci-relay/$file"
done
python3 -m venv /opt/ci-relay/venv
/opt/ci-relay/venv/bin/pip install --disable-pip-version-check -r "$here/requirements-wss.txt" 'certbot>=5.4,<6'
/opt/ci-relay/venv/bin/python -m py_compile /opt/ci-relay/ci_relay.py /opt/ci-relay/ci_relay_ws.py

# Only these two web ports are added. Existing SSH, UDP, and reject rules remain in place.
for port in 80 443; do
    iptables -C INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null || iptables -I INPUT 5 -p tcp --dport "$port" -j ACCEPT
done
if command -v netfilter-persistent >/dev/null; then netfilter-persistent save; fi

conf=/etc/nginx/sites-available/ci-relay
[ ! -f "$conf" ] || cp -p "$conf" "$backup/nginx-ci-relay"
cat > "$conf" <<EOF
server {
    listen 80;
    server_name $ip $name;
    location /.well-known/acme-challenge/ { root /var/www/ci-relay-acme; }
    location / { return 301 https://$ip\$request_uri; }
}
EOF
ln -sfn "$conf" /etc/nginx/sites-enabled/ci-relay
nginx -t
systemctl enable --now nginx
systemctl reload nginx

# Publicly trusted IP certificate; verification is never disabled in the game.
/opt/ci-relay/venv/bin/certbot certonly --non-interactive --agree-tos --register-unsafely-without-email \
    --preferred-profile shortlived --webroot -w /var/www/ci-relay-acme --ip-address "$ip" -d "$name" --cert-name ci-relay-ip --expand
cat >> "$conf" <<EOF
server {
    listen 443 ssl;
    server_name $ip $name;
    ssl_certificate /etc/letsencrypt/live/ci-relay-ip/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/ci-relay-ip/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    tcp_nodelay on;
    client_max_body_size 32k;
    location / {
        proxy_pass http://127.0.0.1:7780;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$remote_addr;
        proxy_buffering off;
        proxy_socket_keepalive on;
        proxy_read_timeout 75s;
        proxy_send_timeout 75s;
    }
}
EOF
nginx -t

install -d -m 755 /etc/systemd/system/ci-relay.service.d
cat > /etc/systemd/system/ci-relay.service.d/wss.conf <<'EOF'
[Service]
ExecStart=
ExecStart=/opt/ci-relay/venv/bin/python -u /opt/ci-relay/ci_relay.py --bind 0.0.0.0 --port 7700 --room-ports 7701-7760 --data /var/lib/ci-relay/ranks.json --ws-bind 127.0.0.1 --ws-port 7780
EOF
cat > /etc/systemd/system/ci-relay-cert-renew.service <<'EOF'
[Unit]
Description=Renew Chaos Impact WSS IP certificate
[Service]
Type=oneshot
ExecStart=/opt/ci-relay/venv/bin/certbot renew --quiet --cert-name ci-relay-ip --deploy-hook "/usr/sbin/nginx -t && /usr/bin/systemctl reload nginx"
EOF
cat > /etc/systemd/system/ci-relay-cert-renew.timer <<'EOF'
[Unit]
Description=Check the short-lived WSS certificate twice daily
[Timer]
OnCalendar=*-*-* 00,12:15:00
RandomizedDelaySec=1800
Persistent=true
[Install]
WantedBy=timers.target
EOF
systemctl daemon-reload
systemctl enable --now ci-relay-cert-renew.timer
systemctl restart ci-relay
systemctl reload nginx
systemctl is-active ci-relay nginx
echo "WSS ready: wss://$name (IP certificate also available)"
echo "Backup of replaced relay files: $backup"
