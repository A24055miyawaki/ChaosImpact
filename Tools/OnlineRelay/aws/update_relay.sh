#!/usr/bin/env bash
# Puts a new ci_relay.py on the AWS relay and restarts it (through Session Manager; no SSH). Upload the new
# ci_relay.py to CloudShell next to this script, then:
#     bash update_relay.sh
# Rooms being played are cut off by the restart.
set -euo pipefail
REGION="${REGION:-ap-northeast-1}"
# CloudShell sets AWS_REGION to the console's own region, and that wins over AWS_DEFAULT_REGION: both are set.
export AWS_REGION="$REGION" AWS_DEFAULT_REGION="$REGION"
echo "== リージョン: $REGION"
NAME="ci-relay"
here="$(cd "$(dirname "$0")" && pwd)"
relay=""
for candidate in "$here/ci_relay.py" "$HOME/ci_relay.py"; do
    [ -f "$candidate" ] && relay="$candidate" && break
done
[ -n "$relay" ] || { echo "ci_relay.py が見つかりません" >&2; exit 1; }

id="$(aws ec2 describe-instances --filters "Name=tag:Name,Values=$NAME" "Name=instance-state-name,Values=running" \
    --query 'Reservations[0].Instances[0].InstanceId' --output text)"
[ "$id" != "None" ] && [ -n "$id" ] || { echo "動いている中継サーバーがありません" >&2; exit 1; }

payload="$(gzip -9c "$relay" | base64 -w0)"
parameters="$(mktemp)"
cat > "$parameters" <<EOF
{"commands":["echo '$payload' | base64 -d | gunzip > /opt/ci-relay/ci_relay.py.new","python3 -m py_compile /opt/ci-relay/ci_relay.py.new","mv /opt/ci-relay/ci_relay.py.new /opt/ci-relay/ci_relay.py","chmod 644 /opt/ci-relay/ci_relay.py","systemctl restart ci-relay","sleep 1","systemctl is-active ci-relay"]}
EOF
command="$(aws ssm send-command --instance-ids "$id" --document-name AWS-RunShellScript --parameters "file://$parameters" \
    --query Command.CommandId --output text)"
rm -f "$parameters"
sleep 5
aws ssm get-command-invocation --command-id "$command" --instance-id "$id" --query '[Status,StandardOutputContent,StandardErrorContent]' --output text
