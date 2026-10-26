#!/usr/bin/env bash
# =============================================================================
# 打一个「直接扔到云服务器就能起」的部署包
#
#   ./deploy/pack.sh                       # -> deploy/mychat-deploy-latest.tar.gz
#   WITH_BASE=1 ./deploy/pack.sh           # 连 mysql:8.0 / redis:8.0 镜像一起带（服务器不能联网时用）
#
# 包内结构（compose 里的相对路径按这个布局写的，别改）：
#   mychat-deploy/
#     README.md
#     images/mychat-images-<tag>.tar.gz
#     db/schema.sql            （空库初始化用的完整结构）
#     db/migrations/*.sql
#     deploy/docker-compose.yml
#     deploy/.env.example
#     deploy/migrate.sh
#     deploy/configs/*.ini.tmpl
#     deploy/verifyserver/config.json.tmpl
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${MYCHAT_TAG:-latest}"
NAME="mychat-deploy"
OUT="${REPO_ROOT}/deploy/${NAME}-${TAG}.tar.gz"

STAGE_ROOT="$(mktemp -d)"
trap 'rm -rf "${STAGE_ROOT}"' EXIT
STAGE="${STAGE_ROOT}/${NAME}"

mkdir -p "${STAGE}/deploy/configs" "${STAGE}/deploy/verifyserver" "${STAGE}/db" "${STAGE}/images"

cp "${REPO_ROOT}/deploy/docker-compose.yml" "${STAGE}/deploy/"
cp "${REPO_ROOT}/deploy/.env.example"       "${STAGE}/deploy/"
cp "${REPO_ROOT}/deploy/migrate.sh"         "${STAGE}/deploy/"
cp "${REPO_ROOT}/deploy/configs/"*.ini.tmpl "${STAGE}/deploy/configs/"
cp "${REPO_ROOT}/deploy/verifyserver/config.json.tmpl" "${STAGE}/deploy/verifyserver/"
cp -r "${REPO_ROOT}/db/migrations" "${STAGE}/db/"
cp "${REPO_ROOT}/db/schema.sql"          "${STAGE}/db/"
cp "${REPO_ROOT}/deploy/README.md" "${STAGE}/README.md"

images=("mychat/server:${TAG}" "mychat/verifyserver:${TAG}")
if [ "${WITH_BASE:-0}" = "1" ]; then
    images+=("${MYSQL_IMAGE:-mysql:8.0}" "${REDIS_IMAGE:-redis:8.0}")
fi
echo "[pack] 导出镜像：${images[*]}"
docker save "${images[@]}" | gzip -1 > "${STAGE}/images/mychat-images-${TAG}.tar.gz"

echo "[pack] 打包 ${OUT}"
tar -czf "${OUT}" -C "${STAGE_ROOT}" "${NAME}"

ls -lh "${OUT}"
echo
echo "下一步："
echo "  scp \"${OUT}\" root@<服务器IP>:/root/"
echo "服务器上："
echo "  tar xzf ${NAME}-${TAG}.tar.gz && cd ${NAME} && cat README.md"
