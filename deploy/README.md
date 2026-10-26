# MyChat 服务端 Docker 部署包

这是 `GateServer` 仓库 `deploy/` 目录产出的 **离线部署包**：里面只有镜像 tar、compose 文件、
配置模板和迁移 SQL，服务器上不需要编译器，也不需要联网拉业务镜像。

## 一、里面有什么

```
mychat-deploy/
├── README.md                     ← 本文件
├── images/
│   └── mychat-images-latest.tar.gz   ← mychat/server + mychat/verifyserver 两个镜像
├── db/
│   ├── schema.sql                ← 当前完整表结构（只在空库初始化时用）
│   └── migrations/*.sql          ← 增量迁移（只增不改，按文件名顺序应用）
└── deploy/
    ├── docker-compose.yml        ← 7 个容器的编排
    ├── .env.example              ← 变量模板，复制成 .env 再改
    ├── migrate.sh               ← 容器内跑的 schema 迁移任务
    ├── configs/*.ini.tmpl        ← 四个服务的 ini 模板（envsubst 渲染）
    └── verifyserver/config.json.tmpl
```

7 个容器：

| 容器            | 作用                                         | 镜像                  |
|-----------------|----------------------------------------------|-----------------------|
| `mysql`         | MySQL 8.0，库名 `chatserver`                 | `mysql:8.0`           |
| `redis`         | token / 服务器注册表 / 离线消息流            | `redis:8.0`           |
| `migrate`       | 一次性任务：建库建表、跑迁移，跑完退出       | `mysql:8.0`           |
| `verify`        | verifyserver（Node gRPC），发验证码邮件      | `mychat/verifyserver` |
| `status`        | StatusServer：挑一台 ChatServer + 签发 token | `mychat/server`       |
| `gate`          | GateServer：客户端 HTTP 入口                 | `mychat/server`       |
| `chat1` `chat2` | ChatServer ×2：客户端直连 + 互发 gRPC        | `mychat/server`       |

端口：

| 端口              | 服务                      | 要不要公网                 |
|-------------------|---------------------------|----------------------------|
| `20060`           | GateServer HTTP           | **要**（客户端连这个）     |
| `20631` / `20632` | ChatServer1 / ChatServer2 | **要**（登录后客户端直连） |
| `50052`           | StatusServer gRPC         | 不用，只在 compose 网络内  |
| `50051`           | verifyserver gRPC         | 不用                       |
| `20641` / `20642` | ChatServer 之间互发 gRPC  | 不用                       |
| `3306` / `6379`   | MySQL / Redis             | 不用（不映射到宿主机）     |

## 二、服务器准备

Ubuntu 22.04 / 24.04 / 26.04，1 核 2G 起（建议 2 核 4G），已装 Docker 与 compose 插件：

```bash
docker version          # 需要 24+
docker compose version  # 需要 v2
# 没装的话：
# apt update && apt install -y docker.io docker-compose-v2
```

云厂商安全组 / 防火墙放行 **TCP 20060、20631、20632**。如果改了端口，按 `.env` 里的实际值放行。

## 三、第一次部署

### 1. 上传并载入镜像

本机（Windows 的 WSL / Git Bash，或任意 Linux）：

```bash
scp mychat-deploy-latest.tar.gz root@<服务器IP>:/root/
```

服务器上：

```bash
cd /root
tar xzf mychat-deploy-latest.tar.gz && cd mychat-deploy
docker load < images/mychat-images-latest.tar.gz
docker images | grep mychat          # 应看到 mychat/server 和 mychat/verifyserver
```

> 基础镜像 `mysql:8.0` / `redis:8.0` 默认由服务器自己从 Docker Hub 拉。
> 服务器不能联网、或拉不动的话：本机打包时带 `WITH_BASE=1 ./deploy/pack.sh`，
> 或者把 `.env` 里的 `MYSQL_IMAGE` / `REDIS_IMAGE` 改成自己的镜像加速地址。

### 2. 写配置

```bash
cd /root/mychat-deploy
cp deploy/.env.example deploy/.env
vi deploy/.env
```

必须改的三处：

- `PUBLIC_HOST` —— **服务器的公网 IP 或域名**。这个是登录后下发给客户端的聊天服地址，
  填 `127.0.0.1` 只有本机客户端能用，填错会出现「登录成功但连不上聊天服」。
- `MYSQL_ROOT_PASSWORD` / `REDIS_PASSWORD` —— 换成强密码。
- `SMTP_USER` / `SMTP_PASS` —— 163 邮箱账号与授权码，留空则验证码邮件发不出去。

### 3. 起服务

```bash
docker compose -f deploy/docker-compose.yml up -d
docker compose -f deploy/docker-compose.yml ps
```

首次启动顺序是自动的：`mysql`/`redis` 等健康检查 → `migrate` 建库建表（空库会先导入
`db/schema.sql`，再把现有迁移标记为已应用）→ `status`/`chat1`/`chat2`/`gate` 起来。
`migrate` 的退出码必须是 0，否则后面的服务不会启动。

### 4. 冒烟自检

```bash
# GateServer 活着
curl http://127.0.0.1:20060/get_test
# → receive get_test req

# 两台聊天服都注册到 Redis 了（选一台 chat 容器的网络即可）
docker compose -f deploy/docker-compose.yml exec redis \
  redis-cli -a "$(grep REDIS_PASSWORD deploy/.env | cut -d= -f2)" --no-auth-warning hgetall serverlist
# → ChatServer1 0.0.0.0:20631 / ChatServer2 0.0.0.0:20632

# 注册 + 登录（验证码先手塞进 Redis，避免依赖 SMTP）
docker compose -f deploy/docker-compose.yml exec redis \
  redis-cli -a "<REDIS_PASSWORD>" --no-auth-warning setex code_smoke@test.local 600 123456

curl -X POST http://127.0.0.1:20060/user_register -H 'Content-Type: application/json' \
  -d '{"email":"smoke@test.local","user":"冒烟号","password":"abc12345","confirm":"abc12345","verify_code":"123456"}'
# → {"error":0,"uid":"<uid>", ...}

curl -X POST http://127.0.0.1:20060/user_login -H 'Content-Type: application/json' \
  -d '{"user":"<uid>","password":"abc12345","email":"smoke@test.local"}'
# → {"error":0,"host":"<PUBLIC_HOST>","port":"20631","token":"...","uid":"<uid>"}
```

最后一步的 `host`/`port` 就是客户端要连的聊天服地址 —— 如果这里不是公网地址，
说明 `deploy/.env` 的 `PUBLIC_HOST` 没改。

## 四、日常运维

```bash
D="docker compose -f deploy/docker-compose.yml"

$D ps                          # 状态
$D logs -f gate                # 跟日志（status/chat1/chat2/verify/migrate 同理）
$D restart chat1               # 重启单个服务
$D down                        # 停掉全部（数据卷保留）
$D down -v                     # 连数据卷一起删（MySQL/Redis 数据全丢，慎用）
```

日志：Release 镜像里 `SPDLOG_ACTIVE_LEVEL=OFF`，业务 debug 日志基本不输出，
主要靠容器 stdout 的启动信息（渲染出来的 ini、Redis 连接结果）。

备份数据库：

```bash
docker compose -f deploy/docker-compose.yml exec -T mysql \
  mysqldump -uroot -p"$(grep MYSQL_ROOT_PASSWORD deploy/.env | cut -d= -f2)" \
  --single-transaction --databases chatserver > chatserver-$(date +%F).sql
```

### 用 CLion / DataGrip 远程连数据库

compose 里 mysql 只绑宿主机 loopback（`127.0.0.1:${MYSQL_PUBLISH_PORT:-3306}`），公网连不上；在 IDE 里开 SSH 隧道即可：

1. Data Source → MySQL：Host `127.0.0.1`、Port `3306`、User `root`、
   Password 取 `.env` 里的 `MYSQL_ROOT_PASSWORD`、Database `chatserver`
2. SSH/SSL 页 → 勾上 `Use SSH tunnel`：Proxy host 填服务器公网 IP、Port 22、
   Proxy user `root`、认证方式 Password

等价的命令行做法：`ssh -N -L 13306:127.0.0.1:3306 root@<服务器IP>`，然后连 `127.0.0.1:13306`。

注意： **重建或重启 mysql 之后要 `restart gate chat1 chat2`** —— 连接池里握着指向旧容器的连接，
不重启的话 HTTP 接口会回 `1013 SERVICE_UNAVAILABLE`（数据库/Redis 池不可用）。

## 五、更新版本

本机（WSL / Git Bash，仓库根目录）：

```bash
MYCHAT_TAG=$(date +%Y%m%d) ./deploy/build-images.sh   # 构建两个镜像
MYCHAT_TAG=$(date +%Y%m%d) ./deploy/pack.sh           # 打 deploy/mychat-deploy-<tag>.tar.gz
```

服务器上：

```bash
scp mychat-deploy-<tag>.tar.gz root@<服务器IP>:/root/   # 本机执行
cd /root && tar xzf mychat-deploy-<tag>.tar.gz && cd mychat-deploy
docker load < images/mychat-images-<tag>.tar.gz
sed -i 's/^MYCHAT_TAG=.*/MYCHAT_TAG=<tag>/' deploy/.env
docker compose -f deploy/docker-compose.yml up -d      # 只重建镜像变了的容器
```

数据库结构有变化时，把新的 `db/migrations/013_xxx.sql` 一起打进包；`up -d` 会重跑
`migrate`，已应用的版本自动跳过。

## 六、常见问题

**登录成功但客户端连不上聊天服**：`PUBLIC_HOST` 填的不是公网地址，或安全组没放行 `20631/20632`。

**`migrate` 退出 1**：先看 `docker compose logs migrate`。

- `Table 'chatserver.user_friend' doesn't exist` → 库里一半初始化过、`db/schema.sql` 没挂上，
  确认 `db/schema.sql` 在包内（`pack.sh` 会带上）。
- 连不上 MySQL → `mysql` 还没健康，或 `.env` 里的密码和已初始化的数据卷不一致（见下条）。

**改了 MySQL 密码后起不来**：`mysql-data` 卷里的 root 密码是第一次初始化时固化的，
改 `.env` 不会改库里的密码。要么改回旧密码，要么 `down -v` 重建（数据会丢）。

**端口被占（`address already in use`）**：宿主机已有别的进程占用（例如本机开发时跑着的原生
GateServer/ChatServer）。改 `.env` 的 `GATE_PORT` / `CHAT1_PORT` / `CHAT2_PORT` 即可，
注意同步放行安全组，并且 `PUBLIC_HOST` 不需要带端口。

**拉不到 `mysql:8.0` / `redis:8.0`**：把 `.env` 的 `MYSQL_IMAGE` / `REDIS_IMAGE` 换成可用的
镜像加速地址（形如 `docker.m.daocloud.io/library/mysql:8.0`），或用 `WITH_BASE=1` 打包时带上。

**时间不对**：compose 里 MySQL 传了 `--default-time-zone=+08:00`，各容器 `TZ=Asia/Shanghai`；
换时区要同时改这两处。

**中文乱码**：MySQL 启动参数已固定 `utf8mb4` + `utf8mb4_0900_ai_ci`，不要改成别的 collation
（`db/migrations/001_unify_collation.sql` 就是为这件事打的补丁）。

## 七、想在容器里开发（可选，不推荐日常用）

镜像里保留了 `dev` 阶段（带 gdb/clangd/vim/ninja），需要时在本机仓库根目录：

```bash
docker build --target dev -t mychat/dev .
docker run --rm -it -v "$PWD":/src -w /src mychat/dev bash
# 容器内：
cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMYCHAT_BUILD_TESTS=ON
cmake --build /tmp/build --parallel
```

Windows 上更省事的还是直接用 CLion + MSYS2 编本机、只在发布时用 Docker 出 Linux 镜像
（两种情况下源码是同一份，`CMakeLists.txt` 已经跨平台）。
