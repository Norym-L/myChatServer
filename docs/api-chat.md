# 聊天消息接口

> **当前状态**：已实现「发送消息」「新消息通知」「拉取消息」「确认收到（ACK）」； **历史记录与群聊未实现**。
> 消息先写入 Redis Stream；接收方拉取后必须发送 ACK，服务端通过 `XACKDEL` 确认并 **删除**该消息；未 ACK 的消息会留在
> PEL，重连后可再次拉取。

| 层                        | 文件                                                                                                              |
|---------------------------|-------------------------------------------------------------------------------------------------------------------|
| TCP 消息分发              | `src/chatServer/LogicSystem.cpp`（`sendTextMessageHandler` / `syncChatMessageHandler` / `ackChatMessageHandler`） |
| 消息编号                  | `src/public/const.h`（`enum MessageType`）                                                                        |
| 消息结构 / 类型           | `src/public/MessageType.h`（`namespace chat_message`）                                                            |
| 服务端推送                | `src/chatServer/net/CSession.cpp`（`notifyReceiveMessage`）                                                       |
| 离线消息存储 / 拉取 / ACK | `src/public/src/RedisManager.cpp`（`insertMessageToOffline` / `getMessageFromOffline` / `ackAndDelChatMessages`） |
| gRPC 转发                 | `src/chatServer/grpc/ChatServiceImpl.cpp`、`ChatGrpcClient.cpp`                                                   |
| gRPC IDL                  | `proto/message.proto`                                                                                             |

## 1. 能力概览

| 能力               | 状态                                                      |
|--------------------|-----------------------------------------------------------|
| 单聊消息发送       | ✅ 已实现（`1013` / `1014`）                              |
| 在线用户新消息通知 | ✅ 已实现（`1022`，同服推送 / 跨服 gRPC 转发）            |
| 离线消息持久化     | ✅ 已实现（Redis Stream，约保留 7 天）                    |
| 消息拉取           | ✅ 已实现（`1023` / `1024`）                              |
| PEL 重投           | ✅ 已实现（拉取时 PEL 优先，重连后可再次拉取未 ACK 消息） |
| 确认收到（ACK）    | ✅ 已实现（`1025` / `1026`，`XACKDEL`）                   |
| 历史记录           | ❌ 未实现（ACK 后消息即被删除）                           |
| 群聊               | ❌ 未实现（`is_to_group` 恒为 `false`）                   |

## 2. TCP 消息一览

| message_id | 名称                             | 方向                    | 说明                                    |
|------------|----------------------------------|-------------------------|-----------------------------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ`      | 客户端 → 服务端         | 发送消息                                |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP`      | 服务端 → 客户端         | 发送回执（回显 `msg_id`）               |
| `1022`     | `MESSAGE_NOTIFY_RECEIVE_MESSAGE` | 服务端 → 客户端（推送） | 有新消息（不含内容）                    |
| `1023`     | `MESSAGE_SYNC_CHAT_MESSAGE_REQ`  | 客户端 → 服务端         | 拉取消息                                |
| `1024`     | `MESSAGE_SYNC_CHAT_MESSAGE_RSP`  | 服务端 → 客户端         | 拉取回包                                |
| `1025`     | `MESSAGE_ACK_CHAT_MESSAGE_REQ`   | 客户端 → 服务端         | 确认收到消息                            |
| `1026`     | `MESSAGE_ACK_CHAT_MESSAGE_RSP`   | 服务端 → 客户端         | 确认回包                                |
| `1015`     | `MESSAGE_NOTIFY_CHAT_MSG_REQ`    | ——                      | 已定义但**未使用**（实际推送走 `1022`） |

> 帧格式见 [`api-tcp.md`](api-tcp.md)：`[message_id:2][body_len:2][body]`。

## 3. 收发流程

```
发送方                      ChatServer                        接收方
  |  1013 发送消息  -------->  |                                  |
  |                           | 写入 Stream offlinestream_<to>   |
  |  <-------- 1014 回执       |                                  |
  |                           | 对端在线？                        |
  |                           | ---- 1022 新消息通知 ---------->  |
  |                           |                                  |
  |                           | <------- 1023 拉取(count) ------- |
  |                           | ---- 1024 消息列表(含 id) ------> |
  |                           | <------- 1025 确认(ids) --------- |
  |                           | XACKDEL                          |
  |                           | ---- 1026 确认回包 -------------> |
```

- `1014` 的 `error = 0` 只代表 **已写入消息流**，不代表对端已收到。
- 接收方可以在收到 `1022` 后拉取，也可以在登录后主动拉取（`1002` 的 `has_pending` 为 `"1"` 时）。
- 拉取到的消息必须用 `1025` 确认；确认后服务端会删除该消息。

### 3.1 客户端拉取时机

- **登录后**：若 `1002` 的 `has_pending == "1"`，立即发送一次 `1023` 拉取（重连后优先取回未 ACK 的消息）；
  `has_pending == "0"` 也不代表没有新消息（离线期间的消息不在 PEL），建议登录后仍拉取一次；
- 每收到一条 `1022`，本地"待拉取"计数 +1；
- 当 **待拉取计数 > 10**，或 **超过设定的超时时间**（如 2s）仍未拉取时，发送一次 `1023` 拉取；
- `count` 取 `min(待拉取计数, 10)`——服务端单次最多 10 条；
- 服务端 **先返回 PEL 中未 ACK 的消息**，PEL 为空才返回新消息；
- 拿到 `1024` 后逐条处理，并用 `1025` 批量确认（单次最多 10 个 `id`）；
- **必须 ACK 后再拉下一批**：未 ACK 的消息会在下一次拉取时被重复返回；
- 若 `1024` 返回 `count == 10`，说明可能还有积压，ACK 后应继续拉取（建议加短间隔，避免空转）。

> 拉取与确认的 **单次上限均为 10 条**，超过会返回 `1020`（`REQUEST_VALUE_INVALID`）。

## 4. 发送消息（1013 / 1014）

### 4.1 请求 MESSAGE_TEXT_CHAT_MSG_REQ（1013）

```json
{
  "msg_id": "c-0001",
  "to_uid": "10002",
  "type": "1",
  "body": "你好"
}
```

| 字段     | 类型   | 必填 | 说明                                                                |
|----------|--------|------|---------------------------------------------------------------------|
| `msg_id` | string | 是   | **客户端生成**的消息标识，用于回执匹配；服务端原样回显              |
| `to_uid` | string | 是   | 接收方 uid                                                          |
| `type`   | string | 是   | 消息类型：`1` 文本 / `2` 图片 / `3` 文件 / `4` 表情包               |
| `body`   | string | 是   | 消息内容；图片 / 文件 / 表情包的具体编码（URL、文件 ID 等）暂未约定 |

- 发送者取 **当前会话已登录的 uid**（`session->getUserId()`），请求体不需要传 `from_uid`。
- 服务端 **不校验** `to_uid` 是否为好友，也不校验登录态（见 §10）。
- `to_uid` 解析失败、`type` 不是 `1`–`4` → 返回 `1020`。

### 4.2 消息类型（`chat_message::Type`）

| 值  | 枚举      | 含义   |
|-----|-----------|--------|
| `1` | `TEXT`    | 文本   |
| `2` | `PICTURE` | 图片   |
| `3` | `FILE`    | 文件   |
| `4` | `EMOJI`   | 表情包 |

### 4.3 响应 MESSAGE_TEXT_CHAT_MSG_RSP（1014）

```json
{
  "error": 0,
  "msg_id": "c-0001"
}
```

| 字段     | 类型   | 说明                                            |
|----------|--------|-------------------------------------------------|
| `error`  | int    | 见下表                                          |
| `msg_id` | string | **原样回显**请求中的 `msg_id`；成功与失败都会带 |

| error  | 含义                                     |
|--------|------------------------------------------|
| `0`    | 已写入离线消息流（**不代表对端已收到**） |
| `1020` | `to_uid` / `type` 非法                   |

> `msg_id` 不做解析，请求中缺失时回显为空串；回执 **不返回** Redis Stream 生成的 `message_id`（服务端消息编号）。

## 5. 新消息通知（1022）

接收方在线时，服务端主动推送：

```json
{
  "error": 0
}
```

- **不含**发送方的 `msg_id`、服务端 `message_id`、发送者、消息类型或内容，仅表示"你有新消息"。
- 同实例：直接通过会话推送；跨实例：gRPC `ChatService.NotifyChatMessage` 转发到对方实例后推送。
- 接收方离线（Redis `uip_<to_uid>` 不存在）时不推送；消息仍已写入离线消息流。
- 客户端收到后应发送 `1023` 拉取消息。

## 6. 拉取消息（1023 / 1024）

### 6.1 请求 MESSAGE_SYNC_CHAT_MESSAGE_REQ（1023）

```json
{
  "count": "10"
}
```

| 字段    | 类型   | 必填 | 说明                                                                 |
|---------|--------|------|----------------------------------------------------------------------|
| `count` | string | 是   | 本次最多拉取的消息条数，取值 `0`–`10`；解析失败或 `> 10` 返回 `1020` |

- 拉取对象取 **当前会话已登录的 uid**（`session->getUserId()`），请求体不需要传 uid。
- **单次最多拉取 10 条**；`count > 10` 返回 `1020`（`REQUEST_VALUE_INVALID`）。
- 服务端按 **PEL 优先**返回：PEL 非空时只返回 PEL 中的消息，为空才返回新消息；未 ACK 的消息会被重复返回。

### 6.2 响应 MESSAGE_SYNC_CHAT_MESSAGE_RSP（1024）

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

| 字段              | 类型   | 说明                                                         |
|-------------------|--------|--------------------------------------------------------------|
| `error`           | int    | `0` 成功；`1020` `count` 解析失败                            |
| `count`           | string | 本次返回的消息条数                                           |
| `msgs`            | array  | 消息列表；**没有消息时不出现该字段**                         |
| `msgs[].type`     | number | 消息类型 `1`–`4`                                             |
| `msgs[].from_uid` | string | 发送方 uid                                                   |
| `msgs[].body`     | string | 消息内容                                                     |
| `msgs[].id`       | string | 消息编号（Redis Stream ID，`<毫秒>-<序号>`），**ACK 时使用** |

- 服务端 **先读 PEL**（`XREADGROUP ... 0`，已投递但未 ACK 的消息），PEL 为空才读新消息（`XREADGROUP ... >`）。
- PEL 非空时本次只返回 PEL 消息，不混入新消息；ACK 完再拉即进入新消息。
- 未 ACK 的消息会在后续拉取时 **重复返回**（at-least-once），客户端按 `msgs[].id` 去重。
- 按 Stream 中的先后顺序返回。
- 群聊消息当前被跳过（`is_to_group` 恒为 `false`）。

## 7. 确认收到（1025 / 1026）

### 7.1 请求 MESSAGE_ACK_CHAT_MESSAGE_REQ（1025）

```json
{
  "ids": [
    "1734567890123-0",
    "1734567890124-0"
  ]
}
```

| 字段  | 类型            | 必填 | 说明                                                                                                |
|-------|-----------------|------|-----------------------------------------------------------------------------------------------------|
| `ids` | array\<string\> | 是   | 要确认的消息编号列表（来自 `1024` 的 `msgs[].id`），**最多 10 个**；不是数组或超过 10 个返回 `1020` |

### 7.2 响应 MESSAGE_ACK_CHAT_MESSAGE_RSP（1026）

```json
{
  "error": 0
}
```

| error  | 含义                             |
|--------|----------------------------------|
| `0`    | 已执行确认 + 删除                |
| `1020` | `ids` 不是数组，或数量超过 10 个 |

- 服务端 **单次最多确认 10 个 ID**；底层执行 `XACKDEL <stream> <group> ACKED IDS <n> <ids...>`。
- **确认后消息会从 Stream 中删除**，不能再次拉取，也没有历史记录。
- 不存在 / 已确认过的 ID 会被忽略，不会报错。
- `XACKDEL` 需要 Redis 8.2+；低版本上命令会失败，确认不会生效（消息会被一直重投）。

## 8. 消息存储（Redis Stream）

| 项                 | 值                                                                                       |
|--------------------|------------------------------------------------------------------------------------------|
| Stream key（单聊） | `offlinestream_<to_uid>`                                                                 |
| Stream key（群聊） | `offlinestream_group_<to_uid>`（**未启用**）                                             |
| 消费组             | `userxgroup_<uid>`，登录时由 `RedisManager::registUser` 创建（`MKSTREAM`）               |
| 字段               | `from_uid`（十进制字符串）、`type`（`1`–`4`）、`body`                                    |
| 消息编号           | Redis Stream 自动生成的 `<毫秒时间戳>-<序号>`                                            |
| 保留策略           | `XADD ... MINID ~ <now - 7天>-0`，约保留 7 天                                            |
| 拉取               | 先 `XREADGROUP ... STREAMS <stream> 0`（PEL）；为空再 `... STREAMS <stream> >`（新消息） |
| 确认               | `XACKDEL <stream> <group> ACKED IDS <n> <ids...>`（需 Redis 8.2+）                       |

- 消息 **无论对端是否在线都会先写入 Stream**，在线与否只影响是否推送 `1022`。
- 客户端 `msg_id` 只用于发送回执匹配， **不写入 Stream**；落库字段只有 `from_uid` / `type` / `body`。
- 发送失败（Redis 不可用）只写日志， **不会**反映到 `1014` 的 `error`（见 §10）。

## 9. gRPC 接口（ChatService.NotifyChatMessage）

```protobuf
service ChatService
{
  // ...
  rpc NotifyChatMessage(NotifyChatMessageReq) returns (NotifyChatMessageRsp) {}
}

message NotifyChatMessageReq
{
  uint64 toUid = 2;
}

message NotifyChatMessageRsp
{
  int32 error = 1;
}
```

- 调用方：发送方所在 ChatServer；服务方：接收方所在 ChatServer。
- 服务方行为：按 `toUid` 查本实例会话；存在则推送 TCP `1022` 并返回 `0`；不存在返回 `1019`（USER_OUTLINE）。
- 客户端调用超时 `3s`；连接池不可用 / RPC 失败 → `1002`（RPC_FAILED）。
- 对端返回 `1019` 时调用方只记录告警， **不影响**已返回给发送方的 `1014` 成功回执。

## 10. 字段类型与注意事项

### 10.1 字段类型

| 字段                            | 位置                       | JSON 类型                                    |
|---------------------------------|----------------------------|----------------------------------------------|
| `msg_id`                        | `1013` 请求、`1014` 响应   | **string**（原样回显）                       |
| `to_uid`                        | `1013` 请求                | **string**                                   |
| `type`                          | `1013` 请求                | **string**（传数字也能被 `asString()` 兼容） |
| `body`                          | `1013` 请求、`1024` 列表项 | **string**                                   |
| `count`                         | `1023` 请求                | **string**（`0`–`10`）                       |
| `count`                         | `1024` 响应                | **number**                                   |
| `ids`                           | `1025` 请求                | **array\<string\>**（最多 10 个）            |
| `msgs[].type`                   | `1024` 响应                | **number**                                   |
| `msgs[].from_uid` / `msgs[].id` | `1024` 响应                | **string**                                   |
| `from_uid` / `type` / `body`    | Redis Stream 字段          | string（Stream 中统一为字符串）              |
| `message_id`                    | Redis Stream               | string（`<ms>-<seq>`）                       |

### 10.2 当前实现注意事项

1. **PEL 重投（at-least-once）**：拉取时先读 PEL（`XREADGROUP ... 0`），未 ACK 的消息会在下次拉取（包括重连后）再次返回；客户端按
   `msgs[].id` 去重。
2. **ACK 即删除**：`XACKDEL` 后消息不可恢复，没有历史记录；`XACKDEL` 需要 Redis 8.2+。
3. **未校验登录态**：发送 / 拉取 / ACK 都直接使用 `session->getUserId()`，未判断 `uid == 0`。
4. **未校验好友关系**：任意登录用户可向任意 `uid` 发消息。
5. **写入失败被吞掉**：`insertMessageToOffline` 返回 `void`，Redis 写入失败只记日志，`1014` 仍返回 `0`。
6. **`MESSAGE_NOTIFY_CHAT_MSG_REQ`（`1015`）已定义但未使用**，实际推送用 `MESSAGE_NOTIFY_RECEIVE_MESSAGE`（`1022`），两者不要混淆。
7. **群聊未实现**：`is_to_group` 恒为 `false`，群聊 Stream（`offlinestream_group_`）不会被写入，拉取时也会跳过群聊消息。
8. **消息体大小受单帧限制**：`body_len` 上限为 `MAX_LENGTH`（2048 字节），超长消息会在会话层被判定为非法帧并断开连接。
9. **`body` / `msg_id` / `ids[]` 未做类型校验**：`root["body"].asString()`、`root["msg_id"].asString()`、`id.asString()`
   在元素为数组 / 对象时会抛异常，异常会逃逸逻辑线程（与其它 handler 的同类问题）。
10. **单次上限 10 条为硬编码**：拉取 `count > 10` 或确认 `ids` 数量 `> 10` 均返回 `1020`，客户端需自行分批。
11. **拉取 / ACK 失败被吞掉**：`getMessageFromOffline` 在 Redis 异常时返回空列表，`1024` 仍返回 `error = 0`、`count = 0`；
    `ackAndDelChatMessages` 失败也不影响 `1026` 的 `error = 0`。
