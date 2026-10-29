#!/usr/bin/env bash
# Removes everything deploy_relay.sh made on AWS (the VM and its disk, the fixed address, the security group, the
# role, the budget alert), so nothing goes on costing. CloudShell:
#     bash remove_relay.sh
set -euo pipefail
REGION="${REGION:-ap-northeast-1}"
# CloudShell sets AWS_REGION to the console's own region, and that wins over AWS_DEFAULT_REGION: both are set.
export AWS_REGION="$REGION" AWS_DEFAULT_REGION="$REGION"
echo "== リージョン: $REGION"
NAME="ci-relay"
ROLE="ci-relay-ssm"

read -r -p "中継サーバーを削除します。よろしいですか？ (yes と入力) " answer
[ "$answer" = "yes" ] || { echo "やめました"; exit 0; }

ids="$(aws ec2 describe-instances --filters "Name=tag:Name,Values=$NAME" "Name=instance-state-name,Values=pending,running,stopping,stopped" \
    --query 'Reservations[].Instances[].InstanceId' --output text)"
for alloc in $(aws ec2 describe-addresses --filters "Name=tag:Name,Values=$NAME" --query 'Addresses[].AllocationId' --output text); do
    association="$(aws ec2 describe-addresses --allocation-ids "$alloc" --query 'Addresses[0].AssociationId' --output text)"
    [ "$association" != "None" ] && aws ec2 disassociate-address --association-id "$association"
    aws ec2 release-address --allocation-id "$alloc"
    echo "== 固定アドレスを解放: $alloc"
done
if [ -n "$ids" ]; then
    aws ec2 terminate-instances --instance-ids $ids >/dev/null
    echo "== サーバーを削除中: $ids"
    aws ec2 wait instance-terminated --instance-ids $ids
fi
sg="$(aws ec2 describe-security-groups --filters "Name=group-name,Values=$NAME" --query 'SecurityGroups[0].GroupId' --output text)"
if [ "$sg" != "None" ] && [ -n "$sg" ]; then
    aws ec2 delete-security-group --group-id "$sg" && echo "== セキュリティグループを削除: $sg"
fi
if aws iam get-instance-profile --instance-profile-name "$ROLE" >/dev/null 2>&1; then
    aws iam remove-role-from-instance-profile --instance-profile-name "$ROLE" --role-name "$ROLE" 2>/dev/null || true
    aws iam delete-instance-profile --instance-profile-name "$ROLE"
fi
if aws iam get-role --role-name "$ROLE" >/dev/null 2>&1; then
    aws iam detach-role-policy --role-name "$ROLE" --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore 2>/dev/null || true
    aws iam delete-role --role-name "$ROLE"
    echo "== 管理用ロールを削除: $ROLE"
fi
account="$(aws sts get-caller-identity --query Account --output text)"
aws budgets delete-budget --account-id "$account" --budget-name ci-relay-monthly >/dev/null 2>&1 && echo "== 予算アラートを削除" || true
echo "削除しました。数時間後に「請求」画面で費用が増えていないか確認してください。"
