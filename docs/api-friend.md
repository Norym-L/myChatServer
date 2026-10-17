# 好友验证系统接口

好友申请、验证（接受 / 拒绝）、申请列表同步与用户在线查询的完整接口说明。

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

| message_id | 名称 | 方向 | 用途 |
|-----------|------|------|------|
| `1005` | `MESSAGE_ADD_FRIEND_REQ` | 客户端 → 服务端 | 提交好友申请 |
| `1006` | `MESSAGE_ADD_FRIEND_RSP` | 服务端 → 客户端 | 申请结果 |
| `1007` | `MESSAGE_SYNC_FRIEND_REQUEST_REQ` | 客户端 → 服务端 | 拉取好友申请列表 |
| `1008` | `MESSAGE_SYNC_FRIEND_REQUEST_RSP` | 服务端 → 客户端 | 列表回包 |
| `1009` | `MESSAGE_NOTIFY_ADD_FRIEND_REQ` | 服务端 → 客户端（推送） | 收到新的好友申请 |
| `1010` | `MESSAGE_AUTH_FRIEND_REQ` | 客户端 → 服务端 | 接受 / 拒绝好友申请 |
| `1011` | `MESSAGE_AUTH_FRIEND_RSP` | 服务端 → 客户端 | 处理结果 |
| `1012` | `MESSAGE_NOTIFY_AUTH_FRIEND_REQ` | 服务端 → 客户端（推送） | 自己发出的申请被处理 |
| `1018` | `MESSAGE_CHECK_IS_USER_ONLINE_REQ` | 客户端 → 服务端 | 查询用户是否在线 |
| `1019` | `MESSAGE_CHECK_IS_USER_ONLINE_RSP` | 服务端 → 客户端 | 在线状态回包 |

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

> `add_friend_request_id` 为**字符串**（其余编号字段的类型约定见 [§10](#10-字段类型速查)）。

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
{ "error": 0, "add_friend_request_id": 123 }
```

- `add_friend_request_id` 此处为**数字**，与 `1006` 的字符串不同。
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

| error | 含义 | 存储过程返回 |
|-------|------|------------|
| `0` | 处理成功 | `0` |
| `1020` | 请求参数非法（`add_friend_request_id` 解析失败，或 `state` 不是 `1` / `2`） | —— |
| `1011` | 申请不存在，或当前用户无权处理（不是该申请的接收人） | `-1` |
| `1021` | 该申请已被处理过（重复处理） | `-2` |
| `1013` | 数据库不可用 | `-10` / `-11` |

> `add_friend_request_id` 是请求体的**原样回显**（不做解析），编号非法或解析失败时也会返回，请求中缺失该字段时回显为空串；`state` 不回显。处理结果的推送见 §6。

## 6. 申请处理结果通知（1012）

A 在线时由服务端主动推送（无请求、无需回包）：

```json
{ "error": 0, "add_friend_request_id": 123, "state": 1 }
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | 固定为 `0` |
| `add_friend_request_id` | 数字 | 好友申请编号 |
| `state` | int | `1` 对方接受，`2` 对方拒绝 |

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

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` 成功；uid 解析失败时返回 `1020` |
| `count` | string | 申请条数 |
| `apply_infos` | array | 申请列表；**无数据时不出现该字段** |
| `apply_infos[].id` | string | 申请编号 |
| `apply_infos[].from_uid` | string | 申请人 uid |
| `apply_infos[].to_uid` | string | 接收人 uid |
| `apply_infos[].name` | string | 对方昵称 |
| `apply_infos[].verify_info` | string | 验证信息 |
| `apply_infos[].icon` | string | 对方头像 |
| `apply_infos[].sex` | int | `0` 男 / `1` 女 / `2` 未知 |
| `apply_infos[].state` | int | `0` 待处理 / `1` 已接受 / `2` 已拒绝 |
| `apply_infos[].nick` | string | 备注：仅当**当前用户是发送方**且申请时填了 `remark` 时非空；接收方或未填备注时为空串（⚠️ 当前实现取值有误，见 §12） |

- 返回当前用户**相关的全部申请**（我发出的 + 我收到的），按创建时间倒序。
- `name` / `icon` / `sex` 取的是**对方**的资料。
- `nick` 是"我给对方设置的备注"，因此**只在发送方视角**返回值，接收方看到的恒为空串。

## 8. 查询用户在线状态（1018 / 1019）

### 8.1 请求 MESSAGE_CHECK_IS_USER_ONLINE_REQ（1018）

```json
{ "uid": "10002" }
```

### 8.2 响应 MESSAGE_CHECK_IS_USER_ONLINE_RSP（1019）

```json
{ "error": 0, "online": "online" }
```

- `online`：`"online"` 或 `"offline"`（字符串）。
- 判定依据：Redis `uip_<uid>` 是否存在（即该用户 30s 内有过心跳）。
- uid 解析失败时返回 `1020`（`REQUEST_VALUE_INVALID`）。

## 9. gRPC 接口（ChatService）

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

### 9.1 NotifyAddFriend

- 调用方：申请所在 ChatServer。
- 服务方行为：按 `toUid` 查本实例会话；存在则推送 TCP `1009`，返回 `0`；不存在返回 `1019`（USER_OUTLINE）。

### 9.2 ReplyAddFriend

- 调用方：接收人所在 ChatServer。
- 服务方行为：按 `senderUid` 查本实例会话；存在则推送 TCP `1012`，返回 `0`；不存在返回 `1019`。
- 注意：`senderUid` 是**原始申请人**，不是发起本次 RPC 的用户。

### 9.3 调用约定

- 超时 `deadline = 3s`；连接池不可用或 RPC 失败 → 返回 `1002`（RPC_FAILED）。
- 服务端返回 `1019` 表示「对端用户不在线」，但调用方只记录告警，**不影响已经返回给客户端的 `1006` / `1011` 成功结果**——离线用户不会收到推送，但数据库记录已生效。

## 10. 字段类型速查

不同消息里同一个编号字段的类型不一致，客户端务必区分：

| 字段 | 出现位置 | JSON 类型 |
|------|---------|-----------|
| `add_friend_request_id` | `1006` / `1011` 响应、`1010` 请求 | **string** |
| `add_friend_request_id` | `1009` 推送、`1012` 推送 | **number** |
| `id` | `1008` 列表项 | **string** |
| `count` | `1008` 响应 | **string** |
| `state` | `1010` 请求 | **string**（`"1"` / `"2"`） |
| `state` | `1012` 推送、`1008` 列表项 | **number** |
| `uid` / `to_uid` / `from_uid` | 所有请求与响应 | **string** |
| `sex` | `1008` 列表项 | **number** |

## 11. 状态与错误码

### 11.1 FriendApplyStatus

| 值 | 枚举 | 含义 |
|----|------|------|
| `0` | `WAITING` | 待处理 |
| `1` | `ACCEPTED` | 已接受 |
| `2` | `REFUSEED` | 已拒绝 |

### 11.2 新增错误码

| 值 | 枚举 | 含义 |
|----|------|------|
| `1020` | `REQUEST_VALUE_INVALID` | 客户端请求数据非法 |
| `1021` | `DOUBLE_HANDLE_REQUEST` | 重复处理请求 |

完整错误码见 [`error-codes.md`](error-codes.md)。

## 12. 当前实现注意事项（⚠️）

以下与代码当前行为一致，供联调时参考：

1. **编号字段类型不统一**：见 [§10](#10-字段类型速查)。
2. **`getNotifyUserIdInAuthFriendReq` 已实现但未被调用**：`authFriendRequestHandler` 直接使用 `handleFriendRequest` 回填的 `sender_uid`。
3. **文本聊天相关消息（`1013`–`1015`）尚未注册处理函数**，收到会被当作未知消息丢弃。
4. **`1008` 的 `nick` 取错列**：`MySqlDao.cpp:406` 用 `row.at(7)`（`remark`）判空，却取 `row.at(4)`（`message`）作为值，导致发送方拿到的 `nick` 实际是验证信息而不是备注。应改为 `row.at(7).as_string()`。
