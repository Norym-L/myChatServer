# 聊天消息接口

> **当前状态**：已实现「发送消息」与「新消息通知」， **尚未实现消息拉取 / 历史记录接口**。
> 通知只告诉客户端"有新消息"，消息体保存在 Redis Stream 中，但没有任何 TCP 接口把消息取回客户端（见 §6）。

| 层              | 文件                                                                                    |
|-----------------|-----------------------------------------------------------------------------------------|
| TCP 消息分发    | `src/chatServer/LogicSystem.cpp`（`sendTextMessageHandler`）                            |
| 消息编号        | `src/public/const.h`（`enum MessageType`）                                              |
| 消息结构 / 类型 | `src/public/MessageType.h`（`namespace chat_message`）                                  |
| 服务端推送      | `src/chatServer/net/CSession.cpp`（`notifyReceiveMessage`）                             |
| 离线消息存储    | `src/public/src/RedisManager.cpp`（`insertMessageToOffline` / `getMessageFromOffline`） |
| gRPC 转发       | `src/chatServer/grpc/ChatServiceImpl.cpp`、`ChatGrpcClient.cpp`                         |
| gRPC IDL        | `proto/message.proto`                                                                   |

## 1. 能力概览

| 能力                | 状态                                           |
|---------------------|------------------------------------------------|
| 单聊消息发送        | ✅ 已实现（`1013` / `1014`）                   |
| 在线用户新消息通知  | ✅ 已实现（`1022`，同服推送 / 跨服 gRPC 转发） |
| 离线消息持久化      | ✅ 已实现（Redis Stream，约保留 7 天）         |
| 消息拉取 / 历史记录 | ❌ **未实现**                                  |
| 消息已读 / 送达回执 | ❌ 未实现                                      |
| 群聊                | ❌ 未实现（`is_to_group` 恒为 `false`）        |

## 2. TCP 消息一览

| message_id | 名称                             | 方向                    | 说明                                    |
|------------|----------------------------------|-------------------------|-----------------------------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ`      | 客户端 → 服务端         | 发送消息                                |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP`      | 服务端 → 客户端         | 发送回执                                |
| `1022`     | `MESSAGE_NOTIFY_RECEIVE_MESSAGE` | 服务端 → 客户端（推送） | 通知有新消息（不含内容）                |
| `1015`     | `MESSAGE_NOTIFY_CHAT_MSG_REQ`    | ——                      | 已定义但**未使用**（实际推送走 `1022`） |

> 帧格式见 [`api-tcp.md`](api-tcp.md)：`[message_id:2][body_len:2][body]`。

## 3. 发送消息（1013 / 1014）

### 3.1 请求 MESSAGE_TEXT_CHAT_MSG_REQ（1013）

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
- 服务端 **不校验** `to_uid` 是否为好友，也不校验登录态（见 §8）。
- `to_uid` 解析失败、`type` 不是 `1`–`4` → 返回 `1020`。

### 3.2 消息类型（`chat_message::Type`）

| 值  | 枚举      | 含义   |
|-----|-----------|--------|
| `1` | `TEXT`    | 文本   |
| `2` | `PICTURE` | 图片   |
| `3` | `FILE`    | 文件   |
| `4` | `EMOJI`   | 表情包 |

### 3.3 响应 MESSAGE_TEXT_CHAT_MSG_RSP（1014）

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

## 4. 新消息通知（1022）

接收方在线时，服务端主动推送：

```json
{
  "error": 0
}
```

- **不含**发送方的 `msg_id`、服务端 `message_id`、发送者、消息类型或内容，仅表示"你有新消息"。
- 同实例：直接通过会话推送；跨实例：gRPC `ChatService.NotifyChatMessage` 转发到对方实例后推送。
- 接收方离线（Redis `uip_<to_uid>` 不存在）时不推送；消息仍已写入离线消息流。
- 由于没有拉取接口，客户端收到 `1022` 后目前 **无法获取消息内容**（见 §6）。

## 5. 消息存储（Redis Stream）

| 项                 | 值                                                                         |
|--------------------|----------------------------------------------------------------------------|
| Stream key（单聊） | `offlinestream_<to_uid>`                                                   |
| Stream key（群聊） | `offlinestream_group_<to_uid>`（**未启用**）                               |
| 消费组             | `userxgroup_<uid>`，登录时由 `RedisManager::registUser` 创建（`MKSTREAM`） |
| 字段               | `from_uid`（十进制字符串）、`type`（`1`–`4`）、`body`                      |
| 消息编号           | Redis Stream 自动生成的 `<毫秒时间戳>-<序号>`                              |
| 保留策略           | `XADD ... MINID ~ <now - 7天>-0`，约保留 7 天                              |
| 读取方式           | `XREADGROUP GROUP <group> <uid> COUNT n STREAMS <stream> >`                |

- 消息 **无论对端是否在线都会先写入 Stream**，在线与否只影响是否推送 `1022`。
- 客户端 `msg_id` 只用于回执匹配， **不写入 Stream**；落库字段只有 `from_uid` / `type` / `body`。
- 发送失败（Redis 不可用）只写日志， **不会**反映到 `1014` 的 `error`（见 §8）。

## 6. 消息拉取接口（未实现）

当前 `enum MessageType` 中 **没有**消息拉取 / 历史记录的消息编号，客户端没有任何途径取回消息体。

底层已具备部分能力但未被调用：

- `RedisManager::getMessageFromOffline(uid, count)` 已实现按用户读取（`XREADGROUP ... >`），目前无调用方；
- 读取后 **没有 `XACK`**：消息会停留在消费组的 PEL 中，`>` 不会重复投递，PEL 会持续增长；
- 未处理消费组不存在、Stream 不存在等错误（统一返回空列表）。

如果后续补齐，建议新增（ **以下为规划，尚未实现**）：

| message_id | 名称                           | 方向            | body                                                                                                   |
|------------|--------------------------------|-----------------|--------------------------------------------------------------------------------------------------------|
| `1023`     | `MESSAGE_PULL_OFFLINE_MSG_REQ` | 客户端 → 服务端 | `{ "count": "<string>" }`                                                                              |
| `1024`     | `MESSAGE_PULL_OFFLINE_MSG_RSP` | 服务端 → 客户端 | `{ "error": 0, "messages": [ { "message_id": "...", "from_uid": "...", "type": 1, "body": "..." } ] }` |

> 需要同时补上 `XACK`（拉取成功后确认），否则消息无法可靠重投。编号仅为提议，以最终实现为准。

## 7. gRPC 接口（ChatService.NotifyChatMessage）

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

## 8. 字段类型与注意事项

### 8.1 字段类型

| 字段                         | 位置                     | JSON 类型                                    |
|------------------------------|--------------------------|----------------------------------------------|
| `msg_id`                     | `1013` 请求、`1014` 响应 | **string**（原样回显）                       |
| `to_uid`                     | `1013` 请求              | **string**                                   |
| `type`                       | `1013` 请求              | **string**（传数字也能被 `asString()` 兼容） |
| `body`                       | `1013` 请求              | **string**                                   |
| `from_uid` / `type` / `body` | Redis Stream 字段        | string（Stream 中统一为字符串）              |
| `message_id`                 | Redis Stream             | string（`<ms>-<seq>`）                       |

### 8.2 当前实现注意事项

1. **没有拉取接口**：`1022` 不带内容，客户端无法取回消息（§6）。
2. **未校验登录态**：`sendTextMessageHandler` 直接用 `session->getUserId()`，未判断 `uid == 0`，未登录连接也能写入消息（
   `from_uid = 0`）。
3. **未校验好友关系**：任意登录用户可向任意 `uid` 发消息。
4. **写入失败被吞掉**：`insertMessageToOffline` 返回 `void`，Redis 写入失败只记日志，`1014` 仍返回 `0`。
5. **`MESSAGE_NOTIFY_CHAT_MSG_REQ`（`1015`）已定义但未使用**，实际推送用 `MESSAGE_NOTIFY_RECEIVE_MESSAGE`（`1022`），两者不要混淆。
6. **群聊未实现**：`is_to_group` 恒为 `false`，群聊 Stream（`offlinestream_group_`）不会被写入。
7. **消息体大小受单帧限制**：`body_len` 上限为 `MAX_LENGTH`（2048 字节），超长消息会在会话层被判定为非法帧并断开连接。
8. **`body` / `msg_id` 未做类型校验**：`root["body"].asString()` / `root["msg_id"].asString()` 在字段为数组 /
   对象时会抛异常，异常会逃逸逻辑线程（与其它 handler 的同类问题）。
9. **消费组未 ACK**：未来实现拉取接口时必须补 `XACK`，否则 PEL 无限增长且消息不可重投。
