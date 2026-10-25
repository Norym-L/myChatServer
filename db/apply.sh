#!/usr/bin/env bash
# 应用 db/migrations 下尚未执行的迁移（按文件名排序，已记录在 schema_migrations 的会跳过）
#
#   ./db/apply.sh              # 应用到 gateserver-mysql / chatserver
#   MYSQL_DB=chatserver_dryrun ./db/apply.sh     # 应用到演练库
#
# 约定：迁移失败立刻中断（set -e），不会带着半套结构继续跑。

set -euo pipefail

CONTAINER="${MYSQL_CONTAINER:-gateserver-mysql}"
DB_NAME="${MYSQL_DB:-chatserver}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MIG_DIR="$HERE/migrations"

mysql_exec() {
    docker exec -i "$CONTAINER" mysql \
        -u"$MYSQL_USER" -p"$MYSQL_PASSWORD" \
        --default-character-set=utf8mb4 "$@" "$DB_NAME"
}

if ! docker inspect --format '{{.State.Running}}' "$CONTAINER" >/dev/null 2>&1; then
    echo "容器 $CONTAINER 不存在或未运行" >&2
    exit 1
fi

applied="$(mysql_exec -N -B -e 'SELECT `version` FROM `schema_migrations`' 2>/dev/null || true)"

for file in "$MIG_DIR"/*.sql; do
    version="$(basename "$file" .sql)"
    if grep -qxF "$version" <<<"$applied"; then
        echo "跳过  $version"
        continue
    fi
    echo "应用  $version"
    mysql_exec <"$file"
    echo "完成  $version"
done

echo
echo "当前已应用："
mysql_exec -N -B -e 'SELECT `version`, `applied_at` FROM `schema_migrations` ORDER BY `version`' 2>/dev/null
