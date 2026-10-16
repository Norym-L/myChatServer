# gRPC 接口

IDL 定义：`proto/message.proto`（package `message`）。

本次变更**未修改 proto**，字段编号与线格式均保持向后兼容；变更集中在调用方的超时策略与错误处理。

## 1. 服务一览

| 服务 | 方法 | 调用方 → 服务方 | 用途 |
|------|------|----------------|------|
| `VerifyService` | `GetVerifyCode` | GateServer → VerifyServer | 请求下发邮箱验证码 |
| `StatusService` | `GetChatServer` | GateServer → StatusServer | 分配 ChatServer 并签发 token |
| `StatusService` | `Login` | ChatServer → StatusServer | 校验客户端提交的 token |

> `VerifyService` 的服务端实现不在本仓库内。

## 2. VerifyService.GetVerifyCode

```protobuf
rpc GetVerifyCode (GetVerifyReq) returns (GetVerifyRsp);

message GetVerifyReq { string email = 1; }

message GetVerifyRsp {
  int32  error = 1;
  string email = 2;
  string code  = 3;   // 兼容位，服务端通常不回填
}
```

**调用方约定（GateServer）**

- 超时：`deadline = now + 3s`。
- 连接池不可用（获取连接超时/池已关闭）→ 返回 `error = 1002`（RPC_FAILED），**不会抛异常或崩溃**。
- RPC 状态非 OK → 同上返回 `error = 1002`。

## 3. StatusService.GetChatServer

```protobuf
rpc GetChatServer (GetChatServerReq) returns (GetChatServerRsp);

message GetChatServerReq { int32 uid = 1; }

message GetChatServerRsp {
  int32  error = 1;
  string host  = 2;
  string port  = 3;
  string token = 4;
}
```

**调用方约定（GateServer）**

- 超时：`deadline = now + 3s`；连接池不可用或 RPC 失败 → `error = 1002`。

**服务方行为（StatusServer）**

- 采用**最少连接数**策略从配置的 ChatServer 中挑选一台，候选与计数均来自 Redis：
  - **候选集**：优先只考虑存活实例——`serverlist`（hash）中存在该实例名，**且** `serverheartbeat_<name>` 未过期；若没有任何存活实例（全新部署 / Redis 被清空 / 实例尚未完成注册），回落为全部配置实例并记录告警，避免 `/user_login` 整体失败。
  - **连接数**：取 `logincount_<name>`（由实例自身维护、带 30s TTL、随心跳续期），缺失按 `0` 处理。
- 生成 UUID 作为 token，并以 `uid → token` 覆盖写入 Redis（`utoken_<uid>`，同一 uid 重复登录会使旧 token 失效）。
- 启动时会校验 ChatServer 的 `host` / `port` / `name` 是否完整，缺失则拒绝启动。

## 4. StatusService.Login

```protobuf
rpc Login (LoginReq) returns (LoginRsp);

message LoginReq { int32 uid = 1; string token = 2; }

message LoginRsp { int32 error = 1; int32 uid = 2; string token = 3; }
```

**调用方约定（ChatServer）**

- 超时：`deadline = now + 3s`。该调用发生在逻辑线程处理登录消息期间，若对端假死且无超时，会导致整个聊天消息处理链停摆。
- 连接池不可用或 RPC 失败 → `error = 1002`。

**服务方行为（StatusServer）**

| 场景 | `error` |
|------|--------|
| uid 未签发过 token | `1011`（USER_UID_INVALID） |
| token 与服务端记录不一致 | `1012`（USER_TOKEN_INVALID） |
| 校验通过 | `0`，并回填 `uid`、`token` |

> 本次变更：token 由 `emplace`（已存在则静默失败）改为**覆盖写**。修复前同一用户第二次登录时，新 token 永远无法通过校验。

## 5. 客户端连接池约定

| 客户端 | 池大小 | 获取连接超时 | 备注 |
|--------|-------|-------------|------|
| GateServer → VerifyServer | 5 | 2 秒 | 超时返回空，由调用方转为 `1002` |
| GateServer → StatusServer | 5 | 2 秒 | 同上 |
| ChatServer → StatusServer | 5 | 2 秒 | 同上 |

所有 gRPC 通道均使用 `InsecureChannelCredentials`（**明文传输，未启用 TLS**），部署时需通过网络隔离保护。
