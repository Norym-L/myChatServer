# GateServer 接口文档

GateServer 是一个分布式聊天系统，由三个可独立部署的服务与一份公共库组成。

## 1. 服务与端口

| 服务 | 角色 | 可执行文件 | 默认监听 | 配置节 |
|------|------|-----------|---------|--------|
| GateServer | HTTP 网关，对外提供验证码/注册/登录/重置密码 | `GateServer.exe` | `0.0.0.0:8080` | `[GateServer]` |
| StatusServer | gRPC 状态服务：聊天服分配、token 签发与校验 | `StatusServer.exe` | `0.0.0.0:50052` | `[StatusServerForServer]` |
| ChatServer | TCP 聊天服务（单进程内同时启动 chat1 / chat2 两个实例） | `ChatServer.exe` | `127.0.0.1:8090` / `127.0.0.1:8091` | `[ChatServer1]` `[ChatServer2]` |
| VerifyServer | gRPC 验证码服务（**不在本仓库内**，由 GateServer 调用） | —— | `127.0.0.1:50051` | `[VerifyServer]` |

外部依赖：

| 依赖 | 默认地址 | 用途 |
|------|---------|------|
| MySQL | `127.0.0.1:3308`，库名 `chatserver` | 用户注册、密码校验、用户信息查询 |
| Redis | `127.0.0.1:6380` | 存放邮箱验证码，key 前缀 `code_` |

配置来源：可执行文件**当前工作目录**下的 `config.ini`（缺失或字段不全时进程会记录错误并以非 0 退出）。

## 2. 文档索引

| 文档 | 内容 |
|------|------|
| [`api-http.md`](api-http.md) | GateServer 的 5 个 HTTP 接口、公共约定与响应示例 |
| [`api-grpc.md`](api-grpc.md) | 三个 gRPC 服务方法的 IDL、调用方与行为约定 |
| [`api-tcp.md`](api-tcp.md) | ChatServer 的私有 TCP 帧协议与消息定义 |
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
