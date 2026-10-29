#!/usr/bin/env bash
# The relay's state on AWS: the VM, its address, and the relay's latest log (through Session Manager). CloudShell:
#     bash status_relay.sh
set -euo pipefail
REGION="${REGION:-ap-northeast-1}"
# CloudShell sets AWS_REGION to the console's own region, and that wins over AWS_DEFAULT_REGION: both are set.
export AWS_REGION="$REGION" AWS_DEFAULT_REGION="$REGION"
echo "== リージョン: $REGION"
NAME="ci-relay"

instance="$(aws ec2 describe-instances --filters "Name=tag:Name,Values=$NAME" "Name=instance-state-name,Values=pending,running,stopping,stopped" \
    --query 'Reservations[0].Instances[0].[InstanceId,State.Name,PublicIpAddress,InstanceType]' --output text)"
if [ "$instance" = "None" ] || [ -z "$instance" ]; then
    echo "中継サーバーはありません"
    exit 0
fi
read -r id state ip type <<< "$instance"
echo "サーバー: $id ($type)  状態: $state  アドレス: $ip"
if [ "$state" != "running" ]; then
    echo "止まっています。起動するには: aws ec2 start-instances --instance-ids $id"
    exit 0
fi
command="$(aws ssm send-command --instance-ids "$id" --document-name AWS-RunShellScript \
    --parameters 'commands=["systemctl is-active ci-relay","journalctl -u ci-relay -n 25 --no-pager"]' \
    --query Command.CommandId --output text 2>/dev/null || true)"
if [ -z "$command" ]; then
    echo "（ログはまだ取れません：管理エージェントの準備中か、ロールが付いていません）"
    exit 0
fi
sleep 4
aws ssm get-command-invocation --command-id "$command" --instance-id "$id" --query StandardOutputContent --output text
