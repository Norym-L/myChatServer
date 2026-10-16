# 接口变更记录

## 2026-10-17（基线 `1387b28`）

本次为聊天服心跳与实例注册能力，同时修掉一个 Redis 写入缺陷。

### 1. 新增能力

| 编号 | 内容 |
|------|------|
| N1 | **TCP 心跳**：新增 `MESSAGE_HEART_BEAT_REQ`（`1016`）/ `MESSAGE_HEART_BEAT_RSP`（`1017`），客户端周期发送，服务端据此续期在线 key `uip_<uid>`（EX 30s） |
| N2 | **实例自注册**：ChatServer 启动时向 Redis 写入 hash `serverlist`（`name → host:port`）、存活 key `serverheartbeat_<name>`（EX 30s）、连接数 key `logincount_<name>`（EX 30s），后台线程每 10s 续期；退出时摘除 |
| N3 | **存活过滤分发**：StatusServer 分发时只考虑「`serverlist` 中存在该实例 **且** `serverheartbeat_<name>` 未过期」的实例，连接数取 `logincount_<name>`；若没有存活实例则回落为全部配置实例并告警 |

### 2. 行为变更（可能影响既有客户端 / 运维）

| 编号 | 接口 | 变更 |
|------|------|------|
| C1 | TCP 登录消息编号 | **文档修正**：登录请求 `1001`、登录回包 `1002`（历史文档写作 `0` / `1`，与 `const.h` 的 `enum MessageType` 不符；线格式未变） |
| C2 | ChatServer 连接数 | 由「共享 hash `logincount_`，仅登录 +1、无 TTL、仅优雅退出时清理」改为「每实例独立 key `logincount_<name>`，EX 30s、登录 +1 / 断开 -1、随心跳续期」 |
| C3 | 用户在线判定 | `uip_<uid>` 由「登录时写一次」改为「登录写入 + 客户端心跳续期」，30s 无心跳即判离线（他人加好友时不再尝试投递） |

### 3. 缺陷修复

| 编号 | 内容 |
|------|------|
| F1 | `RedisManager::hSet(key, field, value, ex)` 会拼出 `HSET ... EX <n>`（Redis 的 `HSET` 不支持 `EX`），恒返回失败；已移除 `ex` 参数并复用正确的 argv 版 `HSET`。此前 `logincount` 的写入始终失败 |

### 4. 未变动的部分

- `proto/message.proto` 未修改。
- TCP 帧头格式（`[message_id:2][body_len:2][body]`）与消息大小上限未变。
- HTTP 接口与 `enum ErrorCode` 的取值未变（[`error-codes.md`](error-codes.md) 仅补齐了此前遗漏的 `1015` / `1016` / `1018` / `1019` 条目）。

## 2026-09-22（commit `6785e4b`，基线 `4f49d1f`）

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
