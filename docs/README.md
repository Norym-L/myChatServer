# GateServer 接口文档

GateServer 是一个分布式聊天系统，由三个可独立部署的服务与一份公共库组成。

## 1. 服务与端口

| 服务 | 角色 | 可执行文件 | 默认监听 | 配置节 |
|------|------|-----------|---------|--------|
| GateServer | HTTP 网关，对外提供验证码/注册/登录/重置密码 | `GateServer.exe` | `0.0.0.0:8080` | `[GateServer]` |
| StatusServer | gRPC 状态服务：聊天服分配、token 签发与校验 | `StatusServer.exe` | `0.0.0.0:50052` | `[StatusServer]` |
| ChatServer | TCP 聊天服务（**单实例进程**，一个进程只跑一个 chat 实例） | `ChatServer.exe` | `127.0.0.1:8090` / `127.0.0.1:8091` | `[ChatServer1]` `[ChatServer2]` |
| VerifyServer | gRPC 验证码服务（**不在本仓库内**，由 GateServer 调用） | —— | `127.0.0.1:50051` | `[VerifyServer]` |

外部依赖：

| 依赖 | 默认地址 | 用途 |
|------|---------|------|
| MySQL | `127.0.0.1:3308`，库名 `chatserver` | 用户注册、密码校验、用户信息查询 |
| Redis | `127.0.0.1:6380` | 验证码、登录 token、用户在线状态、聊天服注册与连接数（键位见 [1.2 Redis 键位](#12-redis-键位)） |

### 1.1 配置与启动

配置按服务拆分在 `configs/` 下，构建时会被复制到可执行文件所在目录：

| 配置文件 | 给谁用 | 启动命令 |
|---------|-------|---------|
| `gate.ini` | GateServer | `GateServer.exe --config=gate.ini` |
| `status.ini` | StatusServer | `StatusServer.exe --config=status.ini` |
| `chat1.ini` | ChatServer 实例 1（chat1） | `ChatServer.exe --config=chat1.ini` |
| `chat2.ini` | ChatServer 实例 2（chat2） | `ChatServer.exe --config=chat2.ini` |

约定：

- `--config` 支持 `--config=<path>` 与 `--config <path>` 两种写法；**省略时回落为当前工作目录下的 `config.ini`**。
- 路径为相对路径时按**进程当前工作目录**解析；配置文件不存在时进程记录错误并以非 0 退出。
- `chat1.ini` / `chat2.ini` 中必须**恰好有一个** `[ChatServerN]` 段，进程据此确定自身身份；0 个或多于 1 个都会启动失败。
- `status.ini` 里需要保留**全部** ChatServer 的 `host`/`port`/`name`，否则无法分发。

### 1.2 Redis 键位

| 键 | 类型 | TTL | 写入方 | 用途 |
|----|------|-----|--------|------|
| `code_<email>` | string | 由 VerifyServer 决定 | VerifyServer | 邮箱验证码 |
| `utoken_<uid>` | string | 30s | StatusServer | 登录 token；ChatServer 登录帧校验用 |
| `uip_<uid>` | string | 30s | ChatServer | 用户在线所在实例名；客户端心跳（`1016`）续期，过期即视为离线 |
| `ubaseinfo_<uid>` | string(JSON) | 500s | ChatServer | 用户基础信息缓存 |
| `ubaseinfo_name_<name>` | string(JSON) | 500s | ChatServer | 用户名 → uid 列表缓存 |
| `serverlist` | hash | 无 | ChatServer | 字段为实例名，值为 `host:port`（服务列表） |
| `serverheartbeat_<name>` | string | 30s | ChatServer | 实例存活标记 |
| `logincount_<name>` | string | 30s | ChatServer | 实例当前连接数 |

- 后 4 个键由 ChatServer 启动时写入，并由后台线程每 **10s** 续期一次；进程退出时立即摘除，崩溃时 30s 内自动过期。
- StatusServer 分发 ChatServer 时只考虑「`serverlist` 中存在该实例 **且** `serverheartbeat_<name>` 未过期」的实例，连接数取 `logincount_<name>`（缺失按 `0`）；若没有任何存活实例（全新部署 / Redis 被清空），回落为全部配置实例并记录告警。

## 2. 文档索引

| 文档 | 内容 |
|------|------|
| [`api-http.md`](api-http.md) | GateServer 的 5 个 HTTP 接口、公共约定与响应示例 |
| [`api-grpc.md`](api-grpc.md) | 三个 gRPC 服务方法的 IDL、调用方与行为约定 |
| [`api-tcp.md`](api-tcp.md) | ChatServer 的私有 TCP 帧协议与消息定义 |
| [`api-friend.md`](api-friend.md) | 好友验证系统：申请 / 验证 / 通知 / 列表 / 在线查询与 gRPC 转发 |
| [`error-codes.md`](error-codes.md) | 全局错误码表（HTTP 与 gRPC 共用） |
| [`api-changes.md`](api-changes.md) | 接口变更记录：新增 / 修改 / 删除与兼容性说明 |

## 3. 典型调用链

**获取验证码**

1. 客户端 → `POST /get_Verifycode`（GateServer）
2. GateServer → `VerifyService.GetVerifyCode`（VerifyServer，gRPC）
3. VerifyServer 生成验证码并写入 Redis（`code_<email>`），同时通过邮件下发

**用户注册**

1. 客户端 → `POST /user_register`（GateServer）
2. GateServer 从 Redis 读取 `code_<email>` 并校验客户端提交的 `verify_code`
3. GateServer → MySQL 存储过程 `reg_user(email, user, argon2id_password, @result)`，返回 uid

**用户登录**

1. 客户端 → `POST /user_login`（GateServer）
2. GateServer → MySQL 校验 `uid + password`（Argon2id）
3. GateServer → `StatusService.GetChatServer`（StatusServer）：按连接数最少分配一个 ChatServer，并签发 token
4. 客户端携带 `uid + token` 连接该 ChatServer，发送 `message_id = 0` 的登录帧
5. ChatServer → `StatusService.Login`（StatusServer）校验 token，随后回 `message_id = 1` 的响应帧

## 4. 版本与约定

- 语言标准：C++20；构建：CMake。
- 所有对外请求/响应中的错误均通过 `error` 字段传递，**成功为 `0`**，错误码见 [`error-codes.md`](error-codes.md)。
- 本文档描述的是当前主干代码的实际行为，接口变更记录见 [`api-changes.md`](api-changes.md)。
