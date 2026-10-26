# =============================================================================
# verifyserver（Node 版验证码服务，另一个仓库：../verifyserver）
#
# 注意构建上下文不是 verifyserver 仓库，而是它的上一层 C:\dev\MyChat
# （因为本 Dockerfile 和入口脚本都放在 GateServer/deploy 里，
#   Docker 的 COPY 不能跨越构建上下文）：
#
#   docker build -f GateServer/deploy/verifyserver/Dockerfile \
#                -t mychat/verifyserver:latest C:\dev\MyChat
#
# 上下文过滤由同目录的 Dockerfile.dockerignore 负责，只会送进 verifyserver/。
# =============================================================================

ARG NODE_IMAGE=node:22-alpine
FROM ${NODE_IMAGE}

RUN apk add --no-cache gettext

ENV NODE_ENV=production
WORKDIR /app

# 先装依赖再拷源码，改代码时能吃到依赖层缓存
COPY verifyserver/package.json verifyserver/package-lock.json ./
RUN npm ci --omit=dev

COPY verifyserver/src ./src
COPY verifyserver/proto ./proto
COPY verifyserver/const.js ./const.js
# 配置模板放在 GateServer/deploy 下，不往 verifyserver 仓库里塞新文件
COPY GateServer/deploy/verifyserver/config.json.tmpl ./config.json.tmpl

COPY GateServer/deploy/verifyserver/entrypoint.sh /usr/local/bin/mychat-verify-entrypoint.sh
RUN chmod 755 /usr/local/bin/mychat-verify-entrypoint.sh \
    && chown -R node:node /app

USER node
EXPOSE 50051
ENTRYPOINT ["/usr/local/bin/mychat-verify-entrypoint.sh"]
