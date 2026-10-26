#!/usr/bin/env bash
# =============================================================================
# 把镜像打成 tar.gz，方便 scp / rsync 到云服务器再 docker load
#
#   ./deploy/save-images.sh                 # -> deploy/mychat-images-latest.tar.gz
#   ./deploy/save-images.sh /tmp/foo.tar.gz # 指定输出路径
#   WITH_BASE=1 ./deploy/save-images.sh     # 连 mysql:8.0 / redis:8.0 一起打包（服务器不能联网时用）
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${MYCHAT_TAG:-latest}"
OUT="${1:-${REPO_ROOT}/deploy/mychat-images-${TAG}.tar.gz}"

images=("mychat/server:${TAG}" "mychat/verifyserver:${TAG}")
if [ "${WITH_BASE:-0}" = "1" ]; then
    images+=("${MYSQL_IMAGE:-mysql:8.0}" "${REDIS_IMAGE:-redis:8.0}")
fi

echo "[save] 打包：${images[*]}"
echo "[save] 输出：${OUT}"

# -1 压缩级别：内网传输/上传云时速度比体积更重要，用 gzip -9 会白白多花几分钟
docker save "${images[@]}" | gzip -1 > "${OUT}"

ls -lh "${OUT}"
echo
echo "上传："
echo "  scp \"${OUT}\" root@<服务器IP>:/root/"
echo "服务器上："
echo "  docker load -i mychat-images-${TAG}.tar.gz"
