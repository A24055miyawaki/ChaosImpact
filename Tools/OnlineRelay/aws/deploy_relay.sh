#!/usr/bin/env bash
# Chaos Impact relay on AWS, all in one go. Run in AWS CloudShell (the console's terminal, already signed in), with
# ci_relay.py uploaded next to this script:
#
#     bash deploy_relay.sh                      # Tokyo, t4g.micro
#     EMAIL=you@example.com bash deploy_relay.sh  # also a monthly budget alert by mail
#
# Makes: a security group opening UDP 7700-7760, a role so the VM can be managed from the browser (Session Manager,
# no SSH keys), one Ubuntu 24.04 Arm VM that installs and starts the relay by itself at its first boot, and a fixed
# public address (Elastic IP). Prints the address to put in the game's RelayServer setting.
set -euo pipefail

REGION="${REGION:-ap-northeast-1}"
TYPE="${TYPE:-t4g.micro}"
NAME="ci-relay"
ROLE="ci-relay-ssm"
# CloudShell sets AWS_REGION to the console's own region, and that wins over AWS_DEFAULT_REGION: both are set.
export AWS_REGION="$REGION" AWS_DEFAULT_REGION="$REGION"
here="$(cd "$(dirname "$0")" && pwd)"
relay=""
for candidate in "$here/ci_relay.py" "$HOME/ci_relay.py"; do
    [ -f "$candidate" ] && relay="$candidate" && break
done
if [ -z "$relay" ]; then
    echo "ci_relay.py が見つかりません。CloudShell の「アクション → ファイルのアップロード」で、このスクリプトと一緒に上げてください。" >&2
    exit 1
fi

echo "== リージョン: $REGION / サーバーの種類: $TYPE"

existing="$(aws ec2 describe-instances \
    --filters "Name=tag:Name,Values=$NAME" "Name=instance-state-name,Values=pending,running,stopping,stopped" \
    --query 'Reservations[].Instances[].InstanceId' --output text)"
if [ -n "$existing" ]; then
    echo "すでに中継サーバーがあります: $existing （作り直すなら先に remove_relay.sh を実行）" >&2
    exit 1
fi

# -- The network: the default VPC, and a security group with only the relay's UDP ports open (no SSH).
vpc="$(aws ec2 describe-vpcs --filters Name=isDefault,Values=true --query 'Vpcs[0].VpcId' --output text)"
if [ "$vpc" = "None" ] || [ -z "$vpc" ]; then
    echo "デフォルト VPC がありません（このリージョンで VPC を消した場合）。" >&2
    exit 1
fi
sg="$(aws ec2 describe-security-groups --filters "Name=group-name,Values=$NAME" "Name=vpc-id,Values=$vpc" \
    --query 'SecurityGroups[0].GroupId' --output text)"
if [ "$sg" = "None" ] || [ -z "$sg" ]; then
    sg="$(aws ec2 create-security-group --group-name "$NAME" --description "Chaos Impact relay (UDP 7700-7760)" \
        --vpc-id "$vpc" --query GroupId --output text)"
    aws ec2 authorize-security-group-ingress --group-id "$sg" --protocol udp --port 7700-7760 --cidr 0.0.0.0/0 >/dev/null
    echo "== セキュリティグループを作成: $sg （UDP 7700-7760）"
fi

# -- A role so the VM can be reached from the browser (Systems Manager Session Manager).
if ! aws iam get-role --role-name "$ROLE" >/dev/null 2>&1; then
    aws iam create-role --role-name "$ROLE" --assume-role-policy-document \
        '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"ec2.amazonaws.com"},"Action":"sts:AssumeRole"}]}' >/dev/null
    aws iam attach-role-policy --role-name "$ROLE" --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore
    echo "== 管理用ロールを作成: $ROLE"
fi
if ! aws iam get-instance-profile --instance-profile-name "$ROLE" >/dev/null 2>&1; then
    aws iam create-instance-profile --instance-profile-name "$ROLE" >/dev/null
    aws iam add-role-to-instance-profile --instance-profile-name "$ROLE" --role-name "$ROLE"
    echo "== ロールの反映を待っています（15秒）"
    sleep 15
fi

# -- What the VM does at its first boot: installs the relay and starts it as a service.
relay_b64="$(gzip -9c "$relay" | base64 -w0)"
userdata="$(mktemp)"
cat > "$userdata" <<EOF
#!/bin/bash
set -e
id cirelay >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin cirelay
mkdir -p /opt/ci-relay
echo '$relay_b64' | base64 -d | gunzip > /opt/ci-relay/ci_relay.py
chmod 644 /opt/ci-relay/ci_relay.py
cat > /etc/systemd/system/ci-relay.service <<'UNIT'
[Unit]
Description=Chaos Impact online relay (password rooms, random and rank matches over the internet)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=cirelay
ExecStart=/usr/bin/python3 -u /opt/ci-relay/ci_relay.py --bind 0.0.0.0 --port 7700 --room-ports 7701-7760 --data /var/lib/ci-relay/ranks.json
Restart=always
RestartSec=3
# The preseason's ranks (ranks.json) live in /var/lib/ci-relay, the one place it may write.
StateDirectory=ci-relay
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
MemoryMax=256M

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now ci-relay
EOF

ami="$(aws ssm get-parameter --name /aws/service/canonical/ubuntu/server/24.04/stable/current/arm64/hvm/ebs-gp3/ami-id \
    --query Parameter.Value --output text)"
echo "== サーバーを作成中（Ubuntu 24.04 Arm, $ami）"
instance="$(aws ec2 run-instances --image-id "$ami" --instance-type "$TYPE" --security-group-ids "$sg" \
    --iam-instance-profile "Name=$ROLE" --user-data "file://$userdata" \
    --block-device-mappings '[{"DeviceName":"/dev/sda1","Ebs":{"VolumeSize":8,"VolumeType":"gp3","DeleteOnTermination":true}}]' \
    --metadata-options HttpTokens=required \
    --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$NAME}]" "ResourceType=volume,Tags=[{Key=Name,Value=$NAME}]" \
    --query 'Instances[0].InstanceId' --output text)"
rm -f "$userdata"
echo "== 起動を待っています: $instance"
aws ec2 wait instance-running --instance-ids "$instance"
zone="$(aws ec2 describe-instances --instance-ids "$instance" --query 'Reservations[0].Instances[0].Placement.AvailabilityZone' --output text)"
echo "== 作成した場所: $zone"

# -- A fixed public address (kept when the VM is stopped and started again).
alloc="$(aws ec2 allocate-address --domain vpc \
    --tag-specifications "ResourceType=elastic-ip,Tags=[{Key=Name,Value=$NAME}]" --query AllocationId --output text)"
aws ec2 associate-address --instance-id "$instance" --allocation-id "$alloc" >/dev/null
ip="$(aws ec2 describe-addresses --allocation-ids "$alloc" --query 'Addresses[0].PublicIp' --output text)"

# -- Optional: a monthly budget that mails when 80% of it is used.
if [ -n "${EMAIL:-}" ]; then
    account="$(aws sts get-caller-identity --query Account --output text)"
    aws budgets create-budget --account-id "$account" \
        --budget '{"BudgetName":"ci-relay-monthly","BudgetLimit":{"Amount":"15","Unit":"USD"},"TimeUnit":"MONTHLY","BudgetType":"COST"}' \
        --notifications-with-subscribers "[{\"Notification\":{\"NotificationType\":\"ACTUAL\",\"ComparisonOperator\":\"GREATER_THAN\",\"Threshold\":80,\"ThresholdType\":\"PERCENTAGE\"},\"Subscribers\":[{\"SubscriptionType\":\"EMAIL\",\"Address\":\"$EMAIL\"}]}]" \
        >/dev/null 2>&1 && echo "== 予算アラートを作成（月15ドルの80%で $EMAIL にメール）" || echo "== 予算アラートは作れませんでした（同名の予算があるかも）"
fi

# -- Is it answering? (the first boot installs it: up to a minute or two)
echo "== 中継サーバーの応答を確認中（最大2分）"
for attempt in $(seq 1 24); do
    if python3 - "$ip" <<'PY'
import json, socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2.0)
s.sendto(json.dumps({"op": "search", "ver": "check", "password": "0000", "rid": 1}).encode(), (sys.argv[1], 7700))
try:
    s.recvfrom(4096)
except socket.timeout:
    sys.exit(1)
PY
    then
        echo
        echo "============================================================"
        echo " 中継サーバーが動いています"
        echo "   アドレス: $ip"
        echo "   ゲームの設定（Config/DefaultGame.ini の [ChaosImpact.Online]）:"
        echo "     RelayServer=$ip:7700"
        echo "============================================================"
        exit 0
    fi
    sleep 5
done
echo "サーバーは作れましたが、まだ応答がありません（$ip）。数分後に status_relay.sh で確認してください。"
