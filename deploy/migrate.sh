#!/usr/bin/env bash
# =============================================================================
# MySQL schema 迁移（在 mysql:8.0 容器里跑，不在宿主机跑）
#
# 与 db/apply.sh 的区别：那份脚本靠 `docker exec gateserver-mysql` 钻进本机容器，
# 这里本身就在容器里，直接连 compose 网络里的 mysql 服务。
#
# 行为：按文件名顺序遍历 /migrations/*.sql，跳过 schema_migrations 里已记录的版本，
#       任一迁移失败立即退出非 0（compose 的 depends_on: service_completed_successfully
#       会挡住后面的 gate/chat 启动）。
# =============================================================================
set -euo pipefail

MYSQL_HOST="${MYSQL_HOST:-mysql}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_DATABASE="${MYSQL_DATABASE:-chatserver}"
MYSQL_MIGRATIONS_DIR="${MYSQL_MIGRATIONS_DIR:-/migrations}"

export MYSQL_PWD="${MYSQL_ROOT_PASSWORD:?MYSQL_ROOT_PASSWORD 未设置}"

mysql_args=(-h "${MYSQL_HOST}" -P "${MYSQL_PORT}" -u root --default-character-set=utf8mb4)

echo "[migrate] 等待 ${MYSQL_HOST}:${MYSQL_PORT} ..."
for i in $(seq 1 120); do
    if mysql "${mysql_args[@]}" -e 'SELECT 1' >/dev/null 2>&1; then
        break
    fi
    if [ "$i" -eq 120 ]; then
        echo "[migrate] 连不上 MySQL，放弃" >&2
        exit 1
    fi
    sleep 1
done

MYSQL_SCHEMA_SQL="${MYSQL_SCHEMA_SQL:-/schema.sql}"

# 库不存在就建（mysql 官方镜像的 MYSQL_DATABASE 通常会建好，这里兜底）
mysql "${mysql_args[@]}" -e "CREATE DATABASE IF NOT EXISTS \`${MYSQL_DATABASE}\` DEFAULT CHARACTER SET utf8mb4;"

echo "[migrate] 目标库：${MYSQL_DATABASE}，迁移目录：${MYSQL_MIGRATIONS_DIR}"

shopt -s nullglob

# 000-012 这些迁移是「增量补丁」，都假设表已经存在（001 直接 ALTER user_friend）。
# 所以空的 chatserver 库不能从 000 开始跑，要先导入 db/schema.sql 这份当前完整结构，
# 再把现有迁移全部标记为已应用 —— schema.sql 正是应用完这些迁移之后导出的。
#
# 「空库」判据用核心表 `user` 是否存在，而不是「表数量为 0」：
# 上一次跑挂在这里时可能已经留下了 schema_migrations，表数量就不是 0 了。
base_tables="$(mysql "${mysql_args[@]}" -N -B \
    -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = '${MYSQL_DATABASE}' AND table_name IN ('user','user_friend','message')")"

if [ "${base_tables}" != "3" ]; then
    if [ ! -f "${MYSQL_SCHEMA_SQL}" ]; then
        echo "[migrate] 库里缺基础表但找不到完整结构 ${MYSQL_SCHEMA_SQL}，无法初始化" >&2
        exit 1
    fi
    echo "[migrate] 库未初始化（基础表缺失）：导入完整结构 ${MYSQL_SCHEMA_SQL} ..."
    if ! mysql "${mysql_args[@]}" "${MYSQL_DATABASE}" < "${MYSQL_SCHEMA_SQL}"; then
        echo "[migrate] 导入 ${MYSQL_SCHEMA_SQL} 失败" >&2
        exit 1
    fi
    version_values=""
    for f in "${MYSQL_MIGRATIONS_DIR}"/*.sql; do
        version_values="${version_values}('$(basename "${f}" .sql)'),"
    done
    version_values="${version_values%,}"
    if [ -n "${version_values}" ]; then
        mysql "${mysql_args[@]}" "${MYSQL_DATABASE}" \
            -e "INSERT INTO \`schema_migrations\` (\`version\`) VALUES ${version_values} ON DUPLICATE KEY UPDATE \`version\` = \`version\`;"
    fi
    echo "[migrate] 初始化完成（记录 $(printf '%s' "${version_values}" | grep -o "')" | wc -l) 个已应用版本）"
fi

# 记账表可能还不存在（000 迁移就会建它），所以这里允许查询失败
applied="$(mysql "${mysql_args[@]}" -N -B "${MYSQL_DATABASE}" \
    -e 'SELECT version FROM schema_migrations' 2>/dev/null || true)"

applied_count=0
skipped_count=0

for f in "${MYSQL_MIGRATIONS_DIR}"/*.sql; do
    version="$(basename "${f}" .sql)"
    if printf '%s\n' "${applied}" | grep -qx -- "${version}"; then
        echo "[migrate] 跳过 ${version}（已应用）"
        skipped_count=$((skipped_count + 1))
        continue
    fi
    echo "[migrate] 应用 ${version} ..."
    if ! mysql "${mysql_args[@]}" "${MYSQL_DATABASE}" < "${f}"; then
        echo "[migrate] 迁移 ${version} 失败" >&2
        exit 1
    fi
    applied_count=$((applied_count + 1))
done

echo "[migrate] 完成：新应用 ${applied_count} 个，跳过 ${skipped_count} 个"

# 打印最终版本，便于在 compose logs 里核对
mysql "${mysql_args[@]}" -N -B "${MYSQL_DATABASE}" \
    -e 'SELECT CONCAT("applied: ", GROUP_CONCAT(version ORDER BY version SEPARATOR ", ")) FROM schema_migrations'
