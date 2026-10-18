# 接口变更记录

> 基线 `75d7459`（提交于 2026-10-17），本次变更 2026-10-18。本文件只记录 **本批**（聊天消息）的新增与变更。

## 1. 变更汇总

| 类别       | 内容                                                                                                                                                                          |
|------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **新增**   | TCP `1022 MESSAGE_NOTIFY_RECEIVE_MESSAGE`；gRPC `ChatService.NotifyChatMessage`；TCP `1023`–`1026` 拉取 / 确认（ACK）；Redis Stream 离线消息存储；PEL 重投（拉取时 PEL 优先） |
| **变更**   | TCP `1013` / `1014` 由「已定义未实现」变为**已实现**（发送消息 + 回执；`1014` 回显客户端 `msg_id`；登录 `1002` 新增 `has_pending`）                                           |
| **删除**   | gRPC `ChatService.SendChatMessage`、`NotifyTextChatMessage` 及其全部消息定义                                                                                                  |
| **未实现** | 历史记录；群聊                                                                                                                                                                |

详细字段与流程见 [`api-chat.md`](api-chat.md)。

## 2. TCP 消息

| message_id | 名称                             | 方向                    | 本批变化                       |
|------------|----------------------------------|-------------------------|--------------------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ`      | 客户端 → 服务端         | **开始处理**（编号沿用，未变） |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP`      | 服务端 → 客户端         | **开始返回**（编号沿用，未变） |
| `1022`     | `MESSAGE_NOTIFY_RECEIVE_MESSAGE` | 服务端 → 客户端（推送） | **新增枚举值**                 |
| `1023`     | `MESSAGE_SYNC_CHAT_MESSAGE_REQ`  | 客户端 → 服务端         | **新增枚举值**                 |
| `1024`     | `MESSAGE_SYNC_CHAT_MESSAGE_RSP`  | 服务端 → 客户端         | **新增枚举值**                 |
| `1025`     | `MESSAGE_ACK_CHAT_MESSAGE_REQ`   | 客户端 → 服务端         | **新增枚举值**                 |
| `1026`     | `MESSAGE_ACK_CHAT_MESSAGE_RSP`   | 服务端 → 客户端         | **新增枚举值**                 |
| `1015`     | `MESSAGE_NOTIFY_CHAT_MSG_REQ`    | ——                      | 保持已定义但**未使用**状态     |

### 2.1 发送消息（1013 / 1014）

请求：

```json
{
  "msg_id": "c-0001",
  "to_uid": "10002",
  "type": "1",
  "body": "你好"
}
```

| 字段     | 类型   | 说明                                                                |
|----------|--------|---------------------------------------------------------------------|
| `msg_id` | string | 客户端生成的消息标识，服务端在回执中原样回显                        |
| `to_uid` | string | 接收方 uid                                                          |
| `type`   | string | `1` 文本 / `2` 图片 / `3` 文件 / `4` 表情包（`chat_message::Type`） |
| `body`   | string | 消息内容                                                            |

响应：

```json
{
  "error": 0,
  "msg_id": "c-0001"
}
```

| error  | 含义                                 |
|--------|--------------------------------------|
| `0`    | 已写入离线消息流（不代表对端已收到） |
| `1020` | `to_uid` / `type` 非法               |

> `msg_id` 为请求体的原样回显，成功与失败都会带；请求缺失时为空串。

- 发送者取会话登录 uid，不需要传 `from_uid`。
- 消息 **无论对端是否在线都会先写入 Redis Stream**。

### 2.2 新消息通知（1022）

接收方在线时推送：

```json
{
  "error": 0
}
```

- 不含消息编号、发送者、类型或内容。
- 同实例直接推送；跨实例经 gRPC `NotifyChatMessage` 转发。
- 接收方离线（`uip_<to_uid>` 不存在）时不推送。

### 2.3 拉取消息（1023 / 1024）

请求：

```json
{
  "count": "10"
}
```

响应：

```json
{
  "error": 0,
  "count": 1,
  "msgs": [
    {
      "type": 1,
      "from_uid": "10002",
      "body": "你好",
      "id": "1734567890123-0"
    }
  ]
}
```

- `count` 为 **数字**（本次返回条数）；`msgs` 无消息时不出现。
- `msgs[].id` 为 Redis Stream 消息编号，ACK 时使用。
- 服务端先读 PEL（`XREADGROUP ... 0`），PEL 为空再读新消息（`XREADGROUP ... >`）；PEL 非空时本次只返回 PEL 消息。
- 未 ACK 的消息会在下次拉取（包括重连后）重复返回，客户端按 `msgs[].id` 去重。
- `count` 请求字段为字符串，取值 `0`–`10`；解析失败或 `> 10` 返回 `1020`。

### 2.4 确认收到（1025 / 1026）

请求：

```json
{
  "ids": [
    "1734567890123-0"
  ]
}
```

响应：

```json
{
  "error": 0
}
```

- `ids` 必须是数组且 **最多 10 个**，否则返回 `1020`。
- 服务端执行 `XACKDEL <stream> <group> ACKED IDS <n> <ids...>`（需 Redis 8.2+）：确认后消息从 Stream 删除，无历史记录。
- 不存在 / 已确认的 ID 会被忽略，不报错。

## 3. gRPC 变更（破坏性）

```protobuf
service ChatService
{
  rpc NotifyAddFriend(AddFriendReq) returns (AddFriendRsp) {}
  rpc ReplyAddFriend(ReplyFriendReq) returns (ReplyFriendRsp) {}
    - rpc SendChatMessage(SendChatMessageReq) returns (SendChatMessageRsp) {}
    - rpc NotifyTextChatMessage(TextChatMessageReq) returns (TextChatMessageRsp) {}
    + rpc NotifyChatMessage(NotifyChatMessageReq) returns (NotifyChatMessageRsp) {}
}

    - message SendChatMessageReq  {uint64 fromUid = 1; uint64 toUid = 2; string message = 3;}
    - message SendChatMessageRsp  {int32 error = 1; uint64 fromUid = 2; uint64 toUid = 3;}
    - message TextChatData        {string messageId = 1; string messageContent = 2;}
    - message TextChatMessageReq  {uint64 fromUid = 1; uint64 toUid = 2; repeated TextChatData textMessage = 3;}
    - message TextChatMessageRsp  {int32 error = 1; uint64 fromUid = 2; uint64 toUid = 3; repeated TextChatData textMessage = 4;}

    + message NotifyChatMessageReq {uint64 toUid = 2;}
    + message NotifyChatMessageRsp {int32 error = 1;}
```

- 调用方：发送方所在 ChatServer；服务方：接收方所在 ChatServer。
- 服务方按 `toUid` 查本实例会话：存在则推送 TCP `1022` 并返回 `0`；不存在返回 `1019`（USER_OUTLINE）。
- 调用超时 `3s`；连接池不可用 / RPC 失败 → `1002`（RPC_FAILED）。
- 对端返回 `1019` 只记录告警，不影响已返回给发送方的 `1014` 成功回执。

## 4. 消息存储

| 项                 | 值                                                                                                  |
|--------------------|-----------------------------------------------------------------------------------------------------|
| Stream key（单聊） | `offlinestream_<to_uid>`                                                                            |
| Stream key（群聊） | `offlinestream_group_<to_uid>`（未启用）                                                            |
| 消费组             | `userxgroup_<uid>`，登录时由 `RedisManager::registUser` 创建（`MKSTREAM`）                          |
| 字段               | `from_uid`、`type`、`body`（均为字符串）                                                            |
| 消息编号           | Redis Stream 生成的 `<毫秒>-<序号>`，通过 `1024` 返回给接收方                                       |
| 保留策略           | `XADD ... MINID ~ <now - 7天>-0`，约 7 天                                                           |
| 读取               | 先 `getMessageFromOffline(..., Pending)`（`XREADGROUP ... 0`），为空再 `New`（`>`），由 `1023` 调用 |
| 确认               | `ackAndDelChatMessages` → `xAckDel`（`XACKDEL ... ACKED`），由 `1025` 调用                          |

## 5. 未实现（后续规划）

| 项       | 说明                                               |
|----------|----------------------------------------------------|
| 历史记录 | ACK 后消息即被 `XDEL` 删除，无法查询历史           |
| 群聊     | `is_to_group` 恒为 `false`，群聊 Stream 不会被写入 |

## 6. 内部变更（不影响对外契约）

| 内容                                                                                                                                |
|-------------------------------------------------------------------------------------------------------------------------------------|
| 新增 `src/public/MessageType.h`：定义 `chat_message::Type`（`TEXT/PICTURE/FILE/EMOJI`）与 `chat_message::Message`                   |
| Redis key 前缀宏从 `const.h` 移至 `RedisManager.h`                                                                                  |
| `RedisManager` 新增 `insertMessageToOffline`、`getMessageFromOffline`、`ackAndDelChatMessages`、`xAdd`、`xGroup` 及 Stream key 前缀 |

## 7. 已知限制

- 发送 / 拉取 / ACK 均未校验登录态（`session->getUserId() == 0` 也能操作）。
- 未校验 `to_uid` 是否为好友。
- Redis 写入失败只记日志，`1014` 仍返回 `0`。
- `body` / `msg_id` / `ids[]` 未做类型校验；单帧上限 2048 字节。
- 拉取为 **at-least-once**：未 ACK 的消息会被再次返回，客户端需按 `id` 去重；ACK（`XACKDEL`）后删除，无历史记录。
- `XACKDEL` 需要 Redis 8.2+，低版本上 ACK 会失败、消息会一直重投。
- 拉取 / 确认 **单次上限均为 10 条**（服务端硬编码），超过返回 `1020`。
