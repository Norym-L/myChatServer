#!/usr/bin/env bash
# =============================================================================
# 构建两个镜像（在能跑 docker 的机器上执行；本项目里就是 WSL）
#
#   ./deploy/build-images.sh
#   BASE_IMAGE=docker.m.daocloud.io/library/ubuntu:26.04 ./deploy/build-images.sh
#
# 产物：
#   mychat/server:<tag>        GateServer / StatusServer / ChatServer 三合一
#   mychat/verifyserver:<tag>  Node 验证码服务
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${MYCHAT_TAG:-latest}"
BASE_IMAGE="${BASE_IMAGE:-ubuntu:26.04}"
# verifyserver 在 GateServer 的隔壁仓库，它的构建上下文是上一层目录
MYCHAT_ROOT="$(dirname "${REPO_ROOT}")"

if [ ! -d "${MYCHAT_ROOT}/verifyserver" ]; then
    echo "[build] 找不到 ${MYCHAT_ROOT}/verifyserver，跳过 verifyserver 镜像" >&2
    echo "[build] 如果只需要 C++ 三个服务，可以设 SKIP_VERIFY=1 重跑" >&2
    [ "${SKIP_VERIFY:-0}" = "1" ] || exit 1
fi

cd "${REPO_ROOT}"

echo "[build] mychat/server:${TAG}（base=${BASE_IMAGE}）"
docker build \
    --build-arg "BASE_IMAGE=${BASE_IMAGE}" \
    -t "mychat/server:${TAG}" \
    .

if [ "${SKIP_VERIFY:-0}" != "1" ]; then
    echo "[build] mychat/verifyserver:${TAG}"
    docker build \
        --build-arg "NODE_IMAGE=${NODE_IMAGE:-node:22-alpine}" \
        -f deploy/verifyserver/Dockerfile \
        -t "mychat/verifyserver:${TAG}" \
        "${MYCHAT_ROOT}"
fi

echo
docker images --format 'table {{.Repository}}:{{.Tag}}\t{{.Size}}\t{{.CreatedSince}}' | grep -E 'REPOSITORY|mychat/' || true
