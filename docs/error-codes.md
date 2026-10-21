# 错误码表

定义位置：`src/public/const.h`（`enum ErrorCode`）。HTTP 响应的 `error` 字段、gRPC 响应的 `error` 字段、TCP 登录响应帧的 `error` 字段**共用同一套编码**。

## 1. 编码表

| 值     | 枚举                                 | 含义                                                  | 典型出现位置                                                                   |
|--------|--------------------------------------|-------------------------------------------------------|--------------------------------------------------------------------------------|
| `0`    | `SUCCESS`                            | 成功                                                  | 全部接口                                                                       |
| `1001` | `ERROR_JSON`                         | JSON 解析失败或必填字段缺失                           | GateServer HTTP 各接口                                                         |
| `1002` | `RPC_FAILED`                         | 下游 gRPC 调用失败（含超时、连接池不可用）            | `/get_Verifycode`、`/user_login`、TCP 登录响应                                 |
| `1003` | `VERIFY_CODE_EXPIRED`                | 验证码不存在或已过期（Redis 无 `code_<email>`）       | `/user_register`、`/reset_password`                                            |
| `1004` | `VERIFY_CODE_ERROR`                  | 验证码错误                                            | `/user_register`、`/reset_password`                                            |
| `1005` | `USER_EXIST`                         | 用户已存在（预留，当前由存储过程返回码映射）          | ——                                                                             |
| `1006` | `PASSWORD_ERROR`                     | 密码错误（或注册时两次密码不一致）                    | `/user_login`、`/user_register`                                                |
| `1007` | `EMAIL_NOT_MATCH`                    | uid 与邮箱不匹配                                      | `/reset_password`                                                              |
| `1008` | `PASSWORD_UP_FAILED`                 | 密码更新失败                                          | `/reset_password`                                                              |
| `1009` | `PASSWORD_INVALID`                   | 密码无效（预留，当前未返回）                          | ——                                                                             |
| `1010` | `REGISTER_USER_FAILED`               | 用户注册失败                                          | `/user_register`                                                               |
| `1011` | `USER_UID_INVALID`                   | 非法/不存在的用户 id                                  | StatusServer `Login`、TCP 登录响应、TCP 编辑资料回包                           |
| `1012` | `USER_TOKEN_INVALID`                 | token 非法或已失效                                    | StatusServer `Login`、TCP 登录响应                                             |
| `1013` | `SERVICE_UNAVAILABLE`                | **依赖服务不可用**（数据库/Redis 连接池超时或不可达） | `/user_login`、`/reset_password`                                               |
| `1014` | `USER_NOT_FOUND`                     | 客户端查询的用户不存在                                | `chatserver LogicSystem::searchUserHandler`                                    |
| `1015` | `CAN_NOT_ADD_YOURSELF_AS_A_FRIEND`   | 不能把自己作为添加好友的对象                          | TCP 添加好友响应                                                               |
| `1016` | `USER_IS_YOUR_FRIEND_BEFOR`          | 添加对象已经是好友                                    | TCP 添加好友响应                                                               |
| `1017` | ——                                   | **未定义**（编号留空，`enum ErrorCode` 中不存在）     | ——                                                                             |
| `1018` | `CHACK_YOUR_RECEIVED_FRIEND_REQUEST` | 对方已向你发起过申请，请检查好友申请列表              | TCP 添加好友响应                                                               |
| `1019` | `USER_OUTLINE`                       | 目标用户离线（本实例无该用户会话）                    | gRPC `ChatService.NotifyAddFriend` / `ReplyAddFriend`                          |
| `1020` | `REQUEST_VALUE_INVALID`              | 客户端请求数据非法（uid / 编号 / state 解析失败）     | TCP 登录、添加好友、同步申请、好友验证、在线查询、设置备注响应                 |
| `1021` | `DOUBLE_HANDLE_REQUEST`              | 重复处理请求（该好友申请已被处理过）                  | TCP 好友验证响应（`1011`）                                                     |
| `1022` | `NOT_FRIEND`                         | 不是好友关系                                          | TCP 发送聊天消息响应（`1014`）、删除好友响应（`1028`）、设置备注回包（`1030`） |

> `enum MessageType` 的 TCP 消息编号（`1001`–`1032`）与这里的 `ErrorCode` 是 **两套独立枚举**，数值相同不代表同一含义
> （例如错误码 `1022` 是 `NOT_FRIEND`，而消息编号 `1022` 是 `MESSAGE_NOTIFY_RECEIVE_MESSAGE`）。
> 好友相关接口见 [`api-friend.md`](api-friend.md)，聊天消息见 [`api-chat.md`](api-chat.md)。

## 2. 变更记录（最新在前）

**2026-10-21（基线 `5524366`）**

- **无新增错误码**：设置好友备注（`1029`）复用 `1020` / `1022` / `1011` / `1013`；编辑用户资料（`1031`）复用 `1011` / `1013`。
- `1022 NOT_FRIEND` 的返回范围扩展到设置好友备注（对方不是好友 / 给自己设置备注）。
- `1011 USER_UID_INVALID` 的返回范围扩展到编辑用户资料（存储过程表示 uid 不存在）。

**2026-10-19（基线 `44ae18a`）**

- **新增** `1022 NOT_FRIEND`：发送聊天消息时目标不是好友、删除好友时关系不存在。

**2026-10-18（基线 `75d7459`）**

- **新增** `1013 SERVICE_UNAVAILABLE`，用于把「后端故障」与「业务失败」区分开。
- **语义调整（可能影响既有客户端判断）**：
  - `/user_login`：数据库不可用时，历史版本返回 `1006 PASSWORD_ERROR`，现在返回 `1013`；
  - `/reset_password`：数据库不可用时，历史版本返回 `1007 EMAIL_NOT_MATCH`，现在返回 `1013`。
  - 客户端若此前把 `1006` / `1007` 同时用于"提示用户重试"，建议改为对 `1013` 做"稍后重试"提示、对 `1006` / `1007` 做"凭据错误"提示。

## 3. 判定原则

| 场景 | 返回 |
|------|------|
| 请求本身不合法（缺字段、JSON 语法错误） | `1001` |
| 业务规则不满足（验证码错、密码错、邮箱不匹配） | 对应的 `1003` / `1004` / `1006` / `1007` |
| 依赖服务不可用（连接池取不到连接、RPC 超时/失败） | `1013`（数据库/Redis）或 `1002`（gRPC） |
| 服务端内部失败（更新/注册未生效） | `1008` / `1010` |

> 排查建议：`1013` / `1002` 应同时检查 MySQL(3308)、Redis(6380)、StatusServer(50052)、VerifyServer(50051) 的可用性；两个连接池的获取连接超时均为 **2 秒**。
