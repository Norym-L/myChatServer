#!/usr/bin/env bash
# =============================================================================
# MyChat 服务容器入口
#
# 服务端的 ConfigManager 只读 ini 文件、不认环境变量，所以这里做两件事：
#   1. 把 deploy/configs/*.ini.tmpl 用 envsubst 渲染成真实 ini；
#   2. 按需等待依赖（mysql / redis / status ...），再 exec 真正的服务进程。
#
# 依赖的环境变量：
#   MYCHAT_BIN               gate | status | chat        （必填）
#   MYCHAT_CONFIG_TEMPLATE   容器内模板路径，如 /opt/mychat/configs/gate.ini.tmpl（必填）
#   MYCHAT_WAIT_FOR          可选，空格分隔的 host:port，起服务前逐个等通
#   MYCHAT_WAIT_TIMEOUT      可选，等待总超时秒数，默认 120
# =============================================================================
set -euo pipefail

: "${MYCHAT_BIN:?MYCHAT_BIN 未设置（gate/status/chat）}"
: "${MYCHAT_CONFIG_TEMPLATE:?MYCHAT_CONFIG_TEMPLATE 未设置（容器内 ini 模板路径，例如 /opt/mychat/configs/gate.ini.tmpl）}"

# ---------------------------------------------------------------- 缺省值
# 没在 compose / .env 里给的变量在这里兜底，避免 envsubst 把 ini 填成空串
export MYSQL_HOST="${MYSQL_HOST:-mysql}"
export MYSQL_PORT="${MYSQL_PORT:-3306}"
export MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
export REDIS_HOST="${REDIS_HOST:-redis}"
export REDIS_PORT="${REDIS_PORT:-6379}"
export REDIS_PASSWORD="${REDIS_PASSWORD:-123456}"
export VERIFY_HOST="${VERIFY_HOST:-verify}"
export VERIFY_PORT="${VERIFY_PORT:-50051}"
export STATUS_HOST="${STATUS_HOST:-status}"
export STATUS_PORT="${STATUS_PORT:-50052}"
export LOG_LEVEL="${LOG_LEVEL:-1}"
# ChatServer 在本容器里监听所有网卡；对外地址由 StatusServer 的 status.ini 决定
export CHAT_BIND_HOST="${CHAT_BIND_HOST:-0.0.0.0}"
export PUBLIC_HOST="${PUBLIC_HOST:-127.0.0.1}"
export CHAT1_PORT="${CHAT1_PORT:-20631}"
export CHAT1_GRPC_PORT="${CHAT1_GRPC_PORT:-20641}"
export CHAT2_PORT="${CHAT2_PORT:-20632}"
export CHAT2_GRPC_PORT="${CHAT2_GRPC_PORT:-20642}"
export PEER_CHAT1_HOST="${PEER_CHAT1_HOST:-chat1}"
export PEER_CHAT2_HOST="${PEER_CHAT2_HOST:-chat2}"
export PEER_CHAT1_NAME="${PEER_CHAT1_NAME:-ChatServer1}"
export PEER_CHAT2_NAME="${PEER_CHAT2_NAME:-ChatServer2}"

case "${MYCHAT_BIN}" in
    gate)   BIN_PATH=/opt/mychat/bin/GateServer ;;
    status) BIN_PATH=/opt/mychat/bin/StatusServer ;;
    chat)   BIN_PATH=/opt/mychat/bin/ChatServer ;;
    *)
        echo "[entrypoint] 未知的 MYCHAT_BIN='${MYCHAT_BIN}'，只支持 gate / status / chat" >&2
        exit 64
        ;;
esac

# ---------------------------------------------------------------- 等依赖
wait_for() {
    local spec="$1" host port deadline=$((SECONDS + ${MYCHAT_WAIT_TIMEOUT:-120}))
    host="${spec%%:*}"
    port="${spec##*:}"
    until (exec 3<>"/dev/tcp/${host}/${port}") 2>/dev/null; do
        if (( SECONDS >= deadline )); then
            echo "[entrypoint] 等 ${host}:${port} 超时（$((SECONDS))s）" >&2
            return 1
        fi
        sleep 1
    done
    echo "[entrypoint] ${host}:${port} 已就绪"
}

for spec in ${MYCHAT_WAIT_FOR:-}; do
    wait_for "${spec}"
done

# ---------------------------------------------------------------- 渲染 ini
OUT_DIR="${MYCHAT_CONFIG_OUT_DIR:-/run/mychat}"
mkdir -p "${OUT_DIR}"
OUT_INI="${OUT_DIR}/$(basename "${MYCHAT_CONFIG_TEMPLATE}" .tmpl)"

envsubst < "${MYCHAT_CONFIG_TEMPLATE}" > "${OUT_INI}"

echo "[entrypoint] 配置模板 ${MYCHAT_CONFIG_TEMPLATE} -> ${OUT_INI}"
# 打出来便于对账；密码一律打码，别让 docker logs 变成明文密码存放处
sed -e 's/^\([[:space:]]*passwd[[:space:]]*=[[:space:]]*\).*/\1***/' \
    -e 's/^\([[:space:]]*password[[:space:]]*=[[:space:]]*\).*/\1***/' \
    -e 's/^/    | /' "${OUT_INI}"

exec "${BIN_PATH}" --config="${OUT_INI}"
