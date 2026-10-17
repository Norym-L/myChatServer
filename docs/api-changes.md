# 接口变更记录

> 基线 `1387b28`。以下汇总自此基线的全部对外接口变更，涵盖提交 `4ecb304`（心跳与实例注册）、`5017e6b`（好友验证）及当前工作区。

## 1. 变更汇总

| 类别 | 内容 |
|------|------|
| **新增** | TCP 心跳 `1016`/`1017`、在线查询 `1018`/`1019`、好友验证 `1010`–`1012`；gRPC `ReplyAddFriend`；Redis 实例自注册；错误码 `1020`/`1021` |
| **修改** | 好友申请/同步/登录的参数校验与错误码；连接数语义；`ReplyFriendReq` 字段；`1008` 响应新增 `nick` |
| **删除** | gRPC `NotifyAuthFriend`、`AuthFriendReq`、`AuthFriendRsp` |
| **修复** | `HSET ... EX` 非法命令；`state` 校验恒真；登录数只增不减；`hSet` TTL 参数 |

## 2. 好友验证系统

详细字段与流程见 [`api-friend.md`](api-friend.md)。

### 2.1 TCP 消息

| message_id | 名称 | 方向 | 说明 |
|-----------|------|------|------|
| `1005` | `MESSAGE_ADD_FRIEND_REQ` | 客户端 → 服务端 | 提交好友申请 |
| `1006` | `MESSAGE_ADD_FRIEND_RSP` | 服务端 → 客户端 | 申请结果 |
| `1007` | `MESSAGE_SYNC_FRIEND_REQUEST_REQ` | 客户端 → 服务端 | 拉取好友申请列表 |
| `1008` | `MESSAGE_SYNC_FRIEND_REQUEST_RSP` | 服务端 → 客户端 | 列表回包（新增 `nick`） |
| `1009` | `MESSAGE_NOTIFY_ADD_FRIEND_REQ` | 服务端 → 客户端（推送） | 收到新的好友申请 |
| `1010` | `MESSAGE_AUTH_FRIEND_REQ` | 客户端 → 服务端 | 接受 / 拒绝申请（**新增处理**） |
| `1011` | `MESSAGE_AUTH_FRIEND_RSP` | 服务端 → 客户端 | 处理结果（**新增**） |
| `1012` | `MESSAGE_NOTIFY_AUTH_FRIEND_REQ` | 服务端 → 客户端（推送） | 申请被处理的结果（**新增**） |
| `1018` | `MESSAGE_CHECK_IS_USER_ONLINE_REQ` | 客户端 → 服务端 | 查询用户是否在线（**新增编号**） |
| `1019` | `MESSAGE_CHECK_IS_USER_ONLINE_RSP` | 服务端 → 客户端 | 在线状态回包（**新增编号**） |

### 2.2 行为变更

| 编号 | 接口 | 变更 |
|------|------|------|
| F1 | `1005` 添加好友 | 新增「请求 `uid` 必须等于会话登录 uid」校验，不一致返回 `1011`；`uid` / `to_uid` 解析失败返回 `1020` |
| F2 | `1006` 申请结果 | 错误码映射统一：`-1`/`-2` → `1011`，`-3` → `1015`，`-4` → `1016`，`-5` → `1018`，其余负值（含 `-10`/`-11`）→ `1013` |
| F3 | `1010` / `1011` 处理申请 | 新增接口：请求 `{add_friend_request_id, state}`（均字符串，`state` 为 `1`/`2`）；响应回显 `add_friend_request_id` |
| F4 | `1012` 处理结果推送 | 新增推送 `{error, add_friend_request_id(数字), state(1接受/2拒绝)}`，由 gRPC `ReplyAddFriend` 跨实例转发 |
| F5 | `1007` / `1008` 同步列表 | 新增会话 uid 校验（不一致 `1011`）；`1008` 列表项新增 `nick`（备注，仅发送方视角有值） |

### 2.3 gRPC 变更（破坏性）

```protobuf
// 删除
- rpc NotifyAuthFriend(AuthFriendReq) returns (AuthFriendRsp) {}
- message AuthFriendReq { uint64 fromUid = 1; uint64 toUid = 2; }
- message AuthFriendRsp { int32 error = 1; uint64 fromUid = 2; uint64 toUid = 3; }

// 新增
+ rpc ReplyAddFriend(ReplyFriendReq) returns (ReplyFriendRsp) {}
+ message ReplyFriendReq {
+   uint64 senderUid = 1;            // 原申请人，即需要接收结果通知的人
+   uint64 addFriendRequestId = 2;
+   int32  state = 3;                // 1 接受，2 拒绝
+ }
+ message ReplyFriendRsp { int32 error = 1; }
```

- `NotifyAddFriend` / `ReplyAddFriend`：目标用户不在本实例时返回 `1019`（USER_OUTLINE）。
- 客户端调用超时 `3s`，连接池不可用或 RPC 失败 → `1002`。
- `ReplyFriendReq` 由 `fromUid/toUid/agree` 改为 `senderUid/addFriendRequestId/state`；`ReplyFriendRsp` 移除 `fromUid`/`toUid`。

### 2.4 存储层

- `handleFriendRequest` 增加输出参数 `sender_uid`，存储过程改为 `handle_friend_request(?, ?, ?, @request_id, @error_code)`。
- 新增 `getNotifyUserIdInAuthFriendReq`（调用 `get_request_sender_uid`，当前未被使用）。
- `getFriendRequestListByUid` 改为 `const`；查询补充 `remark` 列，用于填充 `nick`。
- `UidCodec` 新增 `ParseNumber<T>`（基于 `from_chars`，`noexcept`），`ParseUid` 复用它。

## 3. 心跳与实例注册

| 编号 | 内容 |
|------|------|
| N1 | **TCP 心跳**：新增 `MESSAGE_HEART_BEAT_REQ`（`1016`）/ `MESSAGE_HEART_BEAT_RSP`（`1017`），客户端周期发送，服务端续期在线 key `uip_<uid>`（EX 30s） |
| N2 | **实例自注册**：ChatServer 启动时写入 hash `serverlist`（`name → host:port`）、`serverheartbeat_<name>`（EX 30s）、`logincount_<name>`（EX 30s），后台线程每 10s 续期，退出时摘除 |
| N3 | **存活过滤分发**：StatusServer 只考虑「`serverlist` 存在且 `serverheartbeat_<name>` 未过期」的实例，连接数取 `logincount_<name>`；无存活实例时回落全量并告警 |

## 4. 其他行为变更

| 编号 | 接口 | 变更 |
|------|------|------|
| C1 | TCP 登录消息编号 | **文档修正**：登录请求 `1001`、登录回包 `1002`（历史文档写作 `0` / `1`，与 `const.h` 不符；线格式未变） |
| C2 | ChatServer 连接数 | 由「共享 hash `logincount_`，仅登录 +1、无 TTL」改为「`logincount_<name>`，EX 30s、登录 +1 / 断开 -1、随心跳续期」 |
| C3 | 用户在线判定 | `uip_<uid>` 由「登录时写一次」改为「登录写入 + 客户端心跳续期」，30s 无心跳即判离线 |
| C4 | TCP 登录非法 uid | 非字符串 / 空串 / 含非数字字符的 `uid` 由返回 `1011` 改为 `1020`（`REQUEST_VALUE_INVALID`） |
| C5 | 各 TCP 接口参数解析 | `1005` / `1007` / `1010` / `1018` 中 uid / 编号 / state 解析失败统一返回 `1020`（此前为 `1013` 或未显式设置） |

## 5. 错误码

| 编号 | 内容 |
|------|------|
| E1 | 新增 `1020 REQUEST_VALUE_INVALID`（客户端请求数据非法） |
| E2 | 新增 `1021 DOUBLE_HANDLE_REQUEST`（重复处理好友申请） |
| E3 | `1019 USER_OUTLINE` 的典型位置补充 gRPC `ReplyAddFriend` |
| E4 | `error-codes.md` 补齐此前遗漏的 `1015` / `1016` / `1018` / `1019` 条目（`enum ErrorCode` 取值本身未变） |

## 6. 缺陷修复

| 编号 | 内容 |
|------|------|
| B1 | `RedisManager::hSet(key, field, value, ex)` 拼出 `HSET ... EX <n>`（Redis 不支持），恒失败；已移除 `ex` 参数并复用正确的 argv 版 `HSET` |
| B2 | `authFriendRequestHandler` 的 `state != 1 || state != 2` 恒为真，导致所有 `1010` 请求被拒；改为 `state != 1 && state != 2` |
| B3 | 登录连接数「只增不减」：断开时递减，并改为带 TTL 的每实例 key，崩溃后自动清零 |
| B4 | 移除 `sendFriendRequestHandler` 中未使用的 `user_self_info` 变量 |

## 7. 文档

- 新增 [`api-friend.md`](api-friend.md)：好友验证系统完整接口（申请 / 验证 / 通知 / 列表 / 在线查询 / gRPC）。
- [`README.md`](README.md)：新增「1.2 Redis 键位」表，并补充文档索引。
- [`api-tcp.md`](api-tcp.md)：修正登录消息编号、补充心跳与在线查询、好友接口收敛到 `api-friend.md`。
- [`error-codes.md`](error-codes.md)：补齐 `1015`–`1021`。

## 8. 未变动的部分

- HTTP 接口与 `error` 字段语义未变。
- TCP 帧头格式（`[message_id:2][body_len:2][body]`）与单帧大小上限未变。
- `enum MessageType` / `enum ErrorCode` 的既有取值未变，新增值均追加在末尾。
- 除 `ChatService` 的好友相关 RPC 外，`proto/message.proto` 的其余消息字段编号未变。

## 附：2026-09-22 历史变更（commit `6785e4b`，基线 `4f49d1f`）

本次为代码审查后的缺陷修复批次，共涉及 **30+ 项**修复。其中影响对外接口契约的部分如下。

### 变更汇总

| 类别 | 数量 | 明细 |
|------|------|------|
| **新增** | 3 | HTTP `405` 响应；错误码 `1013`；gRPC 调用超时约定 |
| **修改** | 12 | 5 个 HTTP 接口、3 个 gRPC 方法、TCP 响应帧、StatusServer 启动校验、内部函数签名 |
| **重命名 / 删除** | 4 | `varifycode` → `verify_code`；移除响应中的 `password` / `passwd`；删除 3 个内部死成员 |

### 1. 破坏性变更（客户端需同步调整）

| 编号 | 接口 | 变更 | 旧行为 → 新行为 |
|------|------|------|----------------|
| B1 | `POST /user_register` 响应 | **移除 `password` 字段** | 曾回显明文密码 → 不再返回 |
| B2 | `POST /reset_password` 响应 | **字段重命名 + 移除** | `varifycode`（且恒为空，因读取了不存在的请求字段）→ `verify_code`（正确回显）；同时移除 `passwd` 明文密码回显 |
| B3 | ChatServer TCP `message_id = 1` | **补写 4 字节帧头** | 仅发送 body（客户端无法按协议解析）→ 按 `[id][len][body]` 发送完整帧 |
| B4 | 内部 C++ 签名 | `checkIsAccountMatchEmail` / `checkIsPasswordMatchAccount` 返回类型 `bool` → `MatchResult`（后者同时修正拼写 `chechIsPasswordMAtchAccount`） | 仅影响编译期调用方（`MySqlManager`、GateServer 业务层，已同步） |

### 2. 行为变更（可能影响既有客户端逻辑）

| 编号 | 接口 | 变更 |
|------|------|------|
| C1 | `POST /user_login`、`POST /reset_password` | 数据库不可用时分别返回 `1013`（旧为 `1006` / `1007`），业务失败与后端故障不再混用错误码 |
| C2 | `GET /get_test` 与所有 GET 请求 | 查询参数解码修正：`%41` 正确解为 `A`（旧实现解为十进制文本 `65`）；非法/截断转义（`%ZZ`、`%4`）按原样保留，不再越界读取 |
| C3 | `POST /get_Verifycode`、`/user_register`、`/reset_password`、`/user_login` | 新增必填字段校验：缺字段或 JSON 非法统一返回 `1001`（旧实现会对缺失字段静默取空值继续处理） |
| C4 | StatusServer `Login` | token 由"首次写入、后续静默失败"改为**覆盖写**：同一 uid 重复登录会使旧 token 失效（单会话语义） |
| C5 | 全部 gRPC 调用 | 新增 `deadline = 3s`；连接池不可用/获取连接超时（2 秒）时返回 `1002`，不再空指针崩溃 |
| C6 | StatusServer 启动 | 新增配置校验：`[ChatServer1]` / `[ChatServer2]` 的 `host` / `port` / `name` 缺失即拒绝启动 |

### 3. 新增接口与能力

| 编号 | 内容 |
|------|------|
| N1 | 未支持的 HTTP 方法（`PUT` / `DELETE` / `HEAD` 等）返回 **`405 Method Not Allowed`**；`HEAD` 不带响应体。此前服务端**不返回任何响应**，客户端只能等到 60 秒超时 |
| N2 | 错误码 `1013 SERVICE_UNAVAILABLE` |
| N3 | `docs/` 接口文档（本目录） |

### 4. 未变动的部分（向后兼容）

- `proto/message.proto` 未修改：消息字段、编号、线格式均保持兼容。
- HTTP 成功判定方式未变：仍为 HTTP 200 + `error = 0`。
- TCP 帧头格式未变（`[message_id:2][body_len:2][body]`），本次是**补齐历史实现缺失的帧头**。
- 错误码 0 / 1001–1012 的既有取值与含义未变。

### 5. 内部变更（不影响对外契约）

- 连接池全部改为限时等待（统一 2 秒），超时返回空并由调用方转为错误码。
- Redis 坏连接不再回池，改为销毁后重建。
- `CSession::close()` 与 `send()` 共用锁保护 socket；写失败丢弃发送队列并关闭会话；发送队列积压超阈值改为关闭会话（不再静默丢包）。
- ChatServer 监听端口改由配置读取（此前硬编码 0，导致监听随机端口）。
- 抽出 `src/public/utils/UrlCodec.h`，新增单元测试 `tests/test_UrlCodec.cpp`。
