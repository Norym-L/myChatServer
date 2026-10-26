# ============================================================================
# MyChat 服务端镜像
#
#   一个镜像装三个可执行文件（GateServer / StatusServer / ChatServer），
#   用环境变量 MYCHAT_BIN + MYCHAT_CONFIG_TEMPLATE 选择跑哪一个，
#   compose 里就能用同一份镜像起 gate / status / chat1 / chat2。
#
#   构建：docker build -t mychat/server:latest .
#   只想要开发环境：docker build --target dev -t mychat/server:dev .
# ============================================================================

ARG BASE_IMAGE=ubuntu:26.04

# ----------------------------------------------------------------------------
# builder：完整工具链 + 全部 *-dev 包，编译出三个可执行文件
# ----------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        ca-certificates \
        libboost-all-dev \
        libsodium-dev \
        libssl-dev \
        zlib1g-dev \
        libgrpc++-dev \
        libgrpc-dev \
        protobuf-compiler \
        protobuf-compiler-grpc \
        libprotobuf-dev \
        libspdlog-dev \
        libfmt-dev \
        libjsoncpp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# 只编发布目标：tests 与 ResourceServer 在镜像里没有意义
# （ResourceServer 还缺 configs/resource.ini，根 CMakeLists 里已改成可选目标）
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DMYCHAT_BUILD_TESTS=OFF \
        -DMYCHAT_BUILD_RESOURCE_SERVER=OFF \
        -DMYCHAT_WITH_AWSSDK=OFF \
    && cmake --build /build --parallel "$(nproc)"

# 产物体检：三个可执行文件必须真的存在
RUN test -x /build/GateServer && test -x /build/StatusServer && test -x /build/ChatServer

# 依赖体检：任何动态库 "not found" 都直接让构建失败，
# 别把缺库的镜像带到云服务器上才发现起不来
RUN ! ldd /build/GateServer /build/StatusServer /build/ChatServer | grep -q "not found"

# ----------------------------------------------------------------------------
# dev：给「在容器里开发」留的口子（本机 Windows 开发不走这条路）
#   docker run --rm -it -v "$PWD:/src" mychat/server:dev
# ----------------------------------------------------------------------------
FROM builder AS dev
RUN apt-get update && apt-get install -y --no-install-recommends \
        gdb \
        clangd \
        vim \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
CMD ["sleep", "infinity"]

# ----------------------------------------------------------------------------
# runtime：只装运行时动态库
#   包名里的版本号（1.90 / 1.51 / 32t64 ...）跟着 BASE_IMAGE 的发行版走，
#   换 base 镜像时需要一起确认；下面那条 ldd 检查会兜住漏装。
# ----------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libboost-filesystem1.90.0 \
        libboost-charconv1.90.0 \
        libgrpc++1.51t64 \
        libprotobuf32t64 \
        libspdlog1.15 \
        libfmt10 \
        libjsoncpp26 \
        libsodium23 \
        libssl3t64 \
        ca-certificates \
        gettext-base \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/GateServer /build/StatusServer /build/ChatServer /opt/mychat/bin/
COPY deploy/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

RUN chmod 755 /usr/local/bin/docker-entrypoint.sh \
    && useradd --system --uid 10001 --home-dir /app --shell /usr/sbin/nologin mychat \
    && mkdir -p /app /run/mychat \
    && chown -R mychat:mychat /app /run/mychat

# 运行时再验一次：多阶段 COPY 出来的二进制也得能被这个 base 解析
RUN ! ldd /opt/mychat/bin/GateServer /opt/mychat/bin/StatusServer /opt/mychat/bin/ChatServer | grep -q "not found"

USER mychat
WORKDIR /app

# 入口脚本负责把配置模板按环境变量渲染成 ini，再 exec 真正的服务进程
# （服务端的 ConfigManager 只认 ini 文件，不读环境变量）
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
