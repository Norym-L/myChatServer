# 好友验证系统接口

好友申请、验证（接受 / 拒绝）、好友列表 / 申请列表同步、删除好友与用户在线查询的完整接口说明。

| 层 | 文件 |
|----|------|
| TCP 消息分发 | `src/chatServer/LogicSystem.cpp` |
| TCP 消息编号 / 错误码 | `src/public/const.h` |
| 服务端主动推送 | `src/chatServer/net/CSession.cpp` |
| gRPC 服务端 | `src/chatServer/grpc/ChatServiceImpl.cpp` |
| gRPC 客户端 | `src/chatServer/grpc/ChatGrpcClient.cpp` |
| 存储过程调用 | `src/public/sql/mysql/MySqlDao.cpp` |
| gRPC IDL | `proto/message.proto` |

## 1. 角色与链路

- **A（申请人）**：发起添加好友。
- **B（接收人）**：收到申请后选择接受 / 拒绝。
- 两人可能连在**不同的 ChatServer 实例**上：同实例直接经会话推送，跨实例经 gRPC `ChatService` 转发。
- 在线判定统一读 Redis `uip_<uid>`（30s TTL，由客户端心跳 `1016` 续期）。离线用户收不到推送，但记录已落库，下次拉取列表仍可见。
- 好友关系另有一份 Redis 缓存 `userfriendset_<uid>`（Set，7 天滑动 TTL，不活跃自动过期）：登录时载入，接受申请 / 删除好友时同步，
  用于发送聊天消息前的快速校验，详见 [`api-chat.md`](api-chat.md#11-好友关系校验redis-set)。

### 1.1 申请链路（A → B）

```
A                    A 的 ChatServer                 B 的 ChatServer                   B
|  1005 添加好友 -->   |                                  |                             |
|                     | 写库 send_friend_request          |                             |
|  <-- 1006 结果       |                                  |                             |
|                     | 读 uip_<B> 判断 B 是否在线         |                             |
|                     |-- 同服：直接推送 1009 ----------------------------------------->|
|                     |-- 跨服：gRPC NotifyAddFriend ---->| 推送 1009 ------------------>|
```

### 1.2 验证链路（B → A）

```
B                    B 的 ChatServer                 A 的 ChatServer                   A
|  1010 接受/拒绝 -->  |                                  |                             |
|                     | 写库 handle_friend_request        |                             |
|  <-- 1011 结果       |                                  |                             |
|                     | 读 uip_<A> 判断 A 是否在线         |                             |
|                     |-- 同服：直接推送 1012 ----------------------------------------->|
|                     |-- 跨服：gRPC ReplyAddFriend ----->| 推送 1012 ------------------>|
```

## 2. TCP 消息一览

| message_id | 名称                               | 方向                    | 用途                 |
|------------|------------------------------------|-------------------------|----------------------|
| `1005`     | `MESSAGE_ADD_FRIEND_REQ`           | 客户端 → 服务端         | 提交好友申请         |
| `1006`     | `MESSAGE_ADD_FRIEND_RSP`           | 服务端 → 客户端         | 申请结果             |
| `1007`     | `MESSAGE_SYNC_FRIEND_REQUEST_REQ`  | 客户端 → 服务端         | 拉取好友申请列表     |
| `1008`     | `MESSAGE_SYNC_FRIEND_REQUEST_RSP`  | 服务端 → 客户端         | 列表回包             |
| `1009`     | `MESSAGE_NOTIFY_ADD_FRIEND_REQ`    | 服务端 → 客户端（推送） | 收到新的好友申请     |
| `1010`     | `MESSAGE_AUTH_FRIEND_REQ`          | 客户端 → 服务端         | 接受 / 拒绝好友申请  |
| `1011`     | `MESSAGE_AUTH_FRIEND_RSP`          | 服务端 → 客户端         | 处理结果             |
| `1012`     | `MESSAGE_NOTIFY_AUTH_FRIEND_REQ`   | 服务端 → 客户端（推送） | 自己发出的申请被处理 |
| `1018`     | `MESSAGE_CHECK_IS_USER_ONLINE_REQ` | 客户端 → 服务端         | 查询用户是否在线     |
| `1019`     | `MESSAGE_CHECK_IS_USER_ONLINE_RSP` | 服务端 → 客户端         | 在线状态回包         |
| `1020`     | `MESSAGE_SYNC_FRIEND_LIST_REQ`     | 客户端 → 服务端         | 获取好友列表         |
| `1021`     | `MESSAGE_SYNC_FRIEND_LIST_RSP`     | 服务端 → 客户端         | 好友列表回包         |
| `1027`     | `MESSAGE_DEL_FRIEDN_REQ`           | 客户端 → 服务端         | 删除好友             |
| `1028`     | `MESSAGE_DEL_FRIEDN_RSP`           | 服务端 → 客户端         | 删除结果             |
| `1029`     | `MESSAGE_SET_FRIEND_REMARK_REQ`    | 客户端 → 服务端         | 设置好友备注         |
| `1030`     | `MESSAGE_SET_FRIEND_REMARK_RSP`    | 服务端 → 客户端         | 备注结果             |

> 帧格式见 [`api-tcp.md`](api-tcp.md)：`[message_id:2][body_len:2][body]`。`message_id` 取 `src/public/const.h` 中 `enum MessageType` 的值。

## 3. 提交好友申请（1005 / 1006）

### 3.1 请求 MESSAGE_ADD_FRIEND_REQ（1005）

```json
{
  "uid": "10001",
  "to_uid": "10002",
  "verify_info": "我是 norym",
  "remark": "老同学"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `uid` | string | 是 | 申请人 uid，必须与当前会话已登录 uid 一致，否则返回 `1011` |
| `to_uid` | string | 是 | 接收人 uid |
| `verify_info` | string | 否 | 验证信息（存库） |
| `remark` | string | 否 | 申请人对接收人的备注 |

### 3.2 响应 MESSAGE_ADD_FRIEND_RSP（1006）

**成功**

```json
{ "error": 0, "add_friend_request_id": "123" }
```

> `add_friend_request_id` 为 **字符串**（其余编号字段的类型约定见 [§13](#13-字段类型速查)）。

**失败**（仅含 `error`）

| error | 含义 | 存储过程返回 |
|-------|------|------------|
| `1011` | 申请人或接收人不存在 | `-1` / `-2` |
| `1015` | 不能添加自己为好友 | `-3` |
| `1016` | 对方已经是好友 | `-4` |
| `1018` | 对方已向你发起过申请，请检查好友申请列表 | `-5` |
| `1013` | 数据库不可用 | `-10` / `-11` 及其它未匹配的负值 |

> `uid` 解析失败返回 `1020`；`uid` 与会话 uid 不一致返回 `1011`。

## 4. 收到好友申请通知（1009）

B 在线时由服务端主动推送（无请求、无需回包）：

```json
{ "error": 0, "add_friend_request_id": "123" }
```

- `add_friend_request_id` 此处为 **字符串**（与 `1006` 一致）。
- 只携带编号，不含申请人资料；客户端收到后应调用 **1007** 拉取完整列表（昵称 / 验证信息 / 头像等）。

## 5. 处理好友申请（1010 / 1011）

### 5.1 请求 MESSAGE_AUTH_FRIEND_REQ（1010）

```json
{ "add_friend_request_id": "123", "state": "1" }
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `add_friend_request_id` | string | 好友申请编号（来自 `1009` 推送或 `1008` 列表） |
| `state` | string | `"1"` 接受，`"2"` 拒绝（对应 `FriendApplyStatus`） |

处理人取**当前会话已登录的 uid**（`session->getUserId()`），请求体不需要再传处理人 uid。

### 5.2 响应 MESSAGE_AUTH_FRIEND_RSP（1011）

```json
{ "error": 0, "add_friend_request_id": "123" }
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | 见下表 |
| `add_friend_request_id` | string | **原样回显**请求中的编号；成功与失败都会带 |

| error  | 含义                                                                        | 存储过程返回  |
|--------|-----------------------------------------------------------------------------|---------------|
| `0`    | 处理成功                                                                    | `0`           |
| `1020` | 请求参数非法（`add_friend_request_id` 解析失败，或 `state` 不是 `1` / `2`） | ——            |
| `1011` | 申请不存在，或当前用户无权处理（不是该申请的接收人）                        | `-1`          |
| `1021` | 该申请已被处理过（重复处理）                                                | `-2`          |
| `1013` | 数据库不可用（`-10` / `-11`），**或**好友缓存同步失败                       | `-10` / `-11` |

> `add_friend_request_id` 是请求体的**原样回显**（不做解析），编号非法或解析失败时也会返回，请求中缺失该字段时回显为空串；`state` 不回显。处理结果的推送见 §6。

- 存储过程成功后， **仅接受（`state = 1`） **会同步 Redis 好友集合（`addFriend`）；同步失败返回 `1013`，并且**本次不会向申请人推送
  `1012`**。
  此时数据库中的申请状态已变更，但双方 Redis 好友缓存可能不一致，需等 key 过期（7 天）后重新登录才重建。

## 6. 申请处理结果通知（1012）

A 在线时由服务端主动推送（无请求、无需回包）：

```json
{ "error": 0, "add_friend_request_id": "123", "state": 1 }
```

| 字段                    | 类型   | 说明                       |
|-------------------------|--------|----------------------------|
| `error`                 | int    | 固定为 `0`                 |
| `add_friend_request_id` | string | 好友申请编号               |
| `state`                 | int    | `1` 对方接受，`2` 对方拒绝 |

> 若 `1010` 中同步 Redis 好友集合失败（返回 `1013`），本次不会推送 `1012`；推送失败（对端离线 / gRPC 失败）也不影响 `1011`
> 的成功结果。

## 7. 拉取好友申请列表（1007 / 1008）

### 7.1 请求 MESSAGE_SYNC_FRIEND_REQUEST_REQ（1007）

```json
{ "uid": "10001" }
```

`uid` 必须与当前会话已登录 uid 一致，否则返回 `1011`。

### 7.2 响应 MESSAGE_SYNC_FRIEND_REQUEST_RSP（1008）

```json
{
  "error": 0,
  "count": "2",
  "apply_infos": [
    {
      "id": "123",
      "from_uid": "10002",
      "to_uid": "10001",
      "name": "norym",
      "verify_info": "我是 norym",
      "icon": "http://example.com/icon.png",
      "sex": 0,
      "state": 0,
      "nick": ""
    }
  ]
}
```

| 字段                        | 类型   | 说明                                                                                 |
|-----------------------------|--------|--------------------------------------------------------------------------------------|
| `error`                     | int    | `0` 成功；uid 解析失败时返回 `1020`                                                  |
| `count`                     | string | 申请条数                                                                             |
| `apply_infos`               | array  | 申请列表；**无数据时不出现该字段**                                                   |
| `apply_infos[].id`          | string | 申请编号                                                                             |
| `apply_infos[].from_uid`    | string | 申请人 uid                                                                           |
| `apply_infos[].to_uid`      | string | 接收人 uid                                                                           |
| `apply_infos[].name`        | string | 对方昵称                                                                             |
| `apply_infos[].verify_info` | string | 验证信息                                                                             |
| `apply_infos[].icon`        | string | 对方头像                                                                             |
| `apply_infos[].sex`         | int    | `0` 男 / `1` 女 / `2` 未知                                                           |
| `apply_infos[].state`       | int    | `0` 待处理 / `1` 已接受 / `2` 已拒绝                                                 |
| `apply_infos[].nick`        | string | 备注：仅当**当前用户是发送方**且申请时填了 `remark` 时非空；接收方或未填备注时为空串 |

- 返回当前用户**相关的全部申请**（我发出的 + 我收到的），按创建时间倒序。
- `name` / `icon` / `sex` 取的是**对方**的资料。
- `nick` 是"我给对方设置的备注"，因此**只在发送方视角**返回值，接收方看到的恒为空串。

## 8. 获取好友列表（1020 / 1021）

### 8.1 请求 MESSAGE_SYNC_FRIEND_LIST_REQ（1020）

请求体可为空 JSON `{}`：用户取 **当前会话已登录的 uid**（`session->getUserId()`），不需要再传 uid。

```json
{}
```

### 8.2 响应 MESSAGE_SYNC_FRIEND_LIST_RSP（1021）

```json
{
  "error": 0,
  "count": "1",
  "friends": [
    {
      "uid": "10002",
      "name": "norym",
      "desc": "签名",
      "nick": "老同学",
      "sex": 0,
      "icon": "http://example.com/icon.png"
    }
  ]
}
```

| 字段             | 类型   | 说明                                                                  |
|------------------|--------|-----------------------------------------------------------------------|
| `error`          | int    | `0` 成功；`1011` 会话未登录                                           |
| `count`          | string | 好友条数                                                              |
| `friends`        | array  | 好友列表；**无好友时不出现该字段**                                    |
| `friends[].uid`  | string | 好友 uid                                                              |
| `friends[].name` | string | 好友昵称                                                              |
| `friends[].desc` | string | 好友签名                                                              |
| `friends[].nick` | string | **我给该好友设置的备注**（来自 `user_friend.remark`），未设置时为空串 |
| `friends[].sex`  | int    | `0` 男 / `1` 女 / `2` 未知                                            |
| `friends[].icon` | string | 好友头像                                                              |

- 数据来源：`user_friend` 表中 `uid` 为当前用户的记录，每条再按 `friend_uid` 补齐用户资料。
- 好友账号已不存在（用户资料查不到）时跳过该条，不会导致整体失败。
- 未登录（会话未绑定 uid）时返回 `1011`。
- `count` 按实际返回的 `friends` 条数统计（跳过的无效好友不计入）。

## 9. 删除好友（1027 / 1028）

### 9.1 请求 MESSAGE_DEL_FRIEDN_REQ（1027）

```json
{ "uid": "10002" }
```

| 字段  | 类型   | 必填 | 说明                                                 |
|-------|--------|------|------------------------------------------------------|
| `uid` | string | 是   | 要删除的好友的 uid；删除者取**当前会话已登录的 uid** |

> 枚举名在 `const.h` 中拼写为 `FRIEDN`，与既有代码保持一致，客户端只按数值 `1027` / `1028` 收发即可。

### 9.2 响应 MESSAGE_DEL_FRIEDN_RSP（1028）

```json
{ "error": 0, "uid": "10002" }
```

| 字段    | 类型   | 说明                                                           |
|---------|--------|----------------------------------------------------------------|
| `error` | int    | 见下表                                                         |
| `uid`   | string | **原样回显**请求中的 `uid`；成功与失败都会带，请求缺失时为空串 |

| error  | 含义                                        | 存储过程 `del_friend` 返回 |
|--------|---------------------------------------------|----------------------------|
| `0`    | 删除成功（MySQL 与 Redis 好友缓存均已更新） | `0`                        |
| `1020` | `uid` 解析失败                              | ——                         |
| `1022` | `NOT_FRIEND`：双方不存在好友关系            | `-1`                       |
| `1013` | 数据库 / Redis 不可用                       | `-10` 及其它负值           |

- 删除由存储过程 `del_friend` 完成， **双向删除**双方的好友关系与备注。
- 删除成功后同步 Redis 好友集合（`userfriendset_<uid>`，见 [`api-chat.md`](api-chat.md#11-好友关系校验redis-set)），
  之后双方互相发送消息会返回 `1022`。
- **不通知被删除方**，被删除方的客户端需要自行刷新好友列表才能感知。
- 数据库已删除但 Redis 同步失败时返回 `1013`，此时数据库变更 **不会回滚**，好友缓存会在过期后重建。

## 10. 设置好友备注（1029 / 1030）

### 10.1 请求 MESSAGE_SET_FRIEND_REMARK_REQ（1029）

```json
{
  "id": "c-0001",
  "friend_uid": "10002",
  "remark": "老同学"
}
```

| 字段         | 类型   | 必填 | 说明                                                         |
|--------------|--------|------|--------------------------------------------------------------|
| `id`         | 任意   | 否   | 客户端消息标识，服务端**原样回显**（不做解析，类型保持不变） |
| `friend_uid` | string | 是   | 好友 uid，必须是纯数字字符串，否则返回 `1020`                |
| `remark`     | string | 否   | 新备注；**缺失或为 `null` 时按空串处理，即清空备注**         |

- 设置方取当前会话已登录的 uid（`session->getUserId()`），请求体不需要传自己的 uid。
- 只修改 **单向备注**（`user_friend.remark` 中「我给好友设置的备注」），不影响对方给本人设置的备注。

### 10.2 响应 MESSAGE_SET_FRIEND_REMARK_RSP（1030）

```json
{ "error": 0, "id": "c-0001" }
```

| 字段    | 类型 | 说明                                                             |
|---------|------|------------------------------------------------------------------|
| `error` | int  | 见下表                                                           |
| `id`    | 任意 | **原样回显**请求中的 `id`；成功与失败都会带，请求缺失时为 `null` |

| error  | 含义                                             | 存储过程 `update_friend_remark` 返回 |
|--------|--------------------------------------------------|--------------------------------------|
| `0`    | 设置成功                                         | `0`                                  |
| `1011` | 会话未登录（未绑定 uid）                         | ——                                   |
| `1020` | `friend_uid` 解析失败                            | ——                                   |
| `1022` | `NOT_FRIEND`：对方不是好友，或试图给自己设置备注 | `-1` / `-2`                          |
| `1013` | 数据库不可用                                     | 其它非 `0` 值（如 `-10`）            |

- 纯 MySQL 写操作， **不涉及 Redis**，也不通知对方。
- 成功后客户端需重新拉取好友列表（`1020`）才能看到新备注。

## 11. 查询用户在线状态（1018 / 1019）

### 11.1 请求 MESSAGE_CHECK_IS_USER_ONLINE_REQ（1018）

```json
{ "uid": "10002" }
```

### 11.2 响应 MESSAGE_CHECK_IS_USER_ONLINE_RSP（1019）

```json
{ "error": 0, "online": "online" }
```

- `online`：`"online"` 或 `"offline"`（字符串）。
- 判定依据：Redis `uip_<uid>` 是否存在（即该用户 30s 内有过心跳）。
- uid 解析失败时返回 `1020`（`REQUEST_VALUE_INVALID`）。

## 12. gRPC 接口（ChatService）

```protobuf
service ChatService
{
  rpc NotifyAddFriend(AddFriendReq) returns (AddFriendRsp) {}
  rpc ReplyAddFriend(ReplyFriendReq) returns (ReplyFriendRsp) {}
  rpc SendChatMessage(SendChatMessageReq) returns (SendChatMessageRsp) {}
  rpc NotifyTextChatMessage(TextChatMessageReq) returns (TextChatMessageRsp) {}
}

// 添加好友：A 所在实例 → B 所在实例
message AddFriendReq
{
  uint64 toUid = 1;
  uint64 addFriendRequestId = 2;
}
message AddFriendRsp { int32 error = 1; }

// 回复好友申请：B 所在实例 → A 所在实例
message ReplyFriendReq
{
  uint64 senderUid = 1;            // 原申请人 A 的 uid，即需要接收结果通知的人
  uint64 addFriendRequestId = 2;
  int32  state = 3;                // 1 接受，2 拒绝
}
message ReplyFriendRsp { int32 error = 1; }
```

### 12.1 NotifyAddFriend

- 调用方：申请所在 ChatServer。
- 服务方行为：按 `toUid` 查本实例会话；存在则推送 TCP `1009`，返回 `0`；不存在返回 `1019`（USER_OUTLINE）。

### 12.2 ReplyAddFriend

- 调用方：接收人所在 ChatServer。
- 服务方行为：按 `senderUid` 查本实例会话；存在则推送 TCP `1012`，返回 `0`；不存在返回 `1019`。
- 注意：`senderUid` 是**原始申请人**，不是发起本次 RPC 的用户。

### 12.3 调用约定

- 超时 `deadline = 3s`；连接池不可用或 RPC 失败 → 返回 `1002`（RPC_FAILED）。
- 服务端返回 `1019` 表示「对端用户不在线」，但调用方只记录告警，**不影响已经返回给客户端的 `1006` / `1011` 成功结果**——离线用户不会收到推送，但数据库记录已生效。

## 13. 字段类型速查

各字段的 JSON 类型如下，客户端务必按类型解析：

| 字段                          | 出现位置                          | JSON 类型                    |
|-------------------------------|-----------------------------------|------------------------------|
| `add_friend_request_id`       | `1006` / `1011` 响应、`1010` 请求 | **string**                   |
| `add_friend_request_id`       | `1009` 推送、`1012` 推送          | **string**                   |
| `id`                          | `1008` 列表项                     | **string**                   |
| `count`                       | `1008` 响应                       | **string**                   |
| `count`                       | `1021` 响应                       | **string**                   |
| `nick`                        | `1008` / `1021` 列表项            | **string**                   |
| `state`                       | `1010` 请求                       | **string**（`"1"` / `"2"`）  |
| `state`                       | `1012` 推送、`1008` 列表项        | **number**                   |
| `uid` / `to_uid` / `from_uid` | 所有请求与响应                    | **string**                   |
| `friend_uid`                  | `1029` 请求                       | **string**                   |
| `remark`                      | `1029` 请求                       | **string**（缺失即清空备注） |
| `id`                          | `1029` 请求 / `1030` 回包         | **任意，原样回显**           |
| `sex`                         | `1008` / `1021` 列表项            | **number**                   |

## 14. 状态与错误码

### 14.1 FriendApplyStatus

| 值 | 枚举 | 含义 |
|----|------|------|
| `0` | `WAITING` | 待处理 |
| `1` | `ACCEPTED` | 已接受 |
| `2` | `REFUSEED` | 已拒绝 |

### 14.2 新增错误码

| 值     | 枚举                    | 含义                                         |
|--------|-------------------------|----------------------------------------------|
| `1020` | `REQUEST_VALUE_INVALID` | 客户端请求数据非法                           |
| `1021` | `DOUBLE_HANDLE_REQUEST` | 重复处理请求                                 |
| `1022` | `NOT_FRIEND`            | 不是好友关系（删除好友、发送聊天消息时返回） |

完整错误码见 [`error-codes.md`](error-codes.md)。

## 15. 当前实现注意事项（⚠️）

以下与代码当前行为一致，供联调时参考：

1. **编号字段类型不统一**：见 [§13](#13-字段类型速查)。
2. **`getNotifyUserIdInAuthFriendReq` 已实现但未被调用**：`authFriendRequestHandler` 直接使用 `handleFriendRequest` 回填的 `sender_uid`。
3. **删除好友不通知被删除方**：`1028` 只回给发起删除的客户端，对端需主动刷新好友列表（`1020`）。
4. **备注为单向数据**：设置后不通知对方，也不刷新任何缓存，需重新拉取好友列表（`1020`）。
