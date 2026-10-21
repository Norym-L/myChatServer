# ChatServer TCP 协议

ChatServer 对外提供**裸 TCP**（非 HTTP/WebSocket）私有协议服务，默认监听 `127.0.0.1:8090`（chat1）与 `127.0.0.1:8091`（chat2）。

## 1. 帧格式

所有报文（双向）统一为「4 字节定长头 + 变长体」：

```
+----------------+----------------+---------------------------+
| message_id     | body_len       | body                      |
| 2 字节，大端序  | 2 字节，大端序  | body_len 字节，JSON 文本   |
+----------------+----------------+---------------------------+
```

| 常量 | 值 | 说明 |
| ------ | ---- | ------ |
| `HEAD_TOTAL_LEN` | 4 | 头部总长（2 + 2） |
| `HEAD_ID_LEN` | 2 | message_id 长度 |
| `HEAD_DATA_LEN` | 2 | body_len 长度 |
| `MAX_LENGTH` | 2048 | body 最大长度 |

> ⚠️ **兼容性变更**：历史版本的服务端响应**只发送 body、不写 4 字节帧头**，任何按协议解析的客户端都无法工作。当前实现已按上表写入完整帧头；若客户端此前是"裸读 JSON"，需同步改为先读 4 字节头再读 body。

## 2. 消息定义

| message_id | 名称 | 方向 | body |
| ----------- | ------ | ------ | ------ |
| `1001` | `MESSAGE_CHAT_LOGIN_REQ` | 客户端 → 服务端 | JSON `{ "uid": "<string>", "token": <string> }`，见 2.1 |
| `1002` | `MESSAGE_CHAT_LOGIN_RSP` | 服务端 → 客户端 | JSON，见 2.2 |
| `1016` | `MESSAGE_HEART_BEAT_REQ` | 客户端 → 服务端 | 心跳，body 可为空 `{}`，见 2.3 |
| `1017` | `MESSAGE_HEART_BEAT_RSP` | 服务端 → 客户端 | JSON `{ "error": <int> }`，见 2.3 |

> **编号说明**：所有 `message_id` 与 `src/public/const.h` 的 `enum MessageType` 一一对应，从 `1001` 起递增。历史版本文档曾把登录写成 `0` / `1`，与实现不符，本版按实现修正（登录请求 `1001`、登录回包 `1002`）。搜索 / 好友相关编号见 [§6](#6-实现的接口)。

### 2.1 MESSAGE_CHAT_LOGIN_REQ（1001）

```json
{ "uid": "10001", "token": "1f0c9d3a-...." }
```

- `uid` / `token` 取自 `POST /user_login` 的响应；`uid` 为**字符串**，服务端内部按 `std::uint64_t` 处理。
- 非字符串、空串或含非数字字符的 `uid` → 直接回 `1020`（`REQUEST_VALUE_INVALID`，不查询 Redis / MySQL）。
- 服务端将 `uid` 与 `token` 提交给 StatusServer 的 `StatusService.Login` 校验。

### 2.2 MESSAGE_CHAT_LOGIN_RSP（1002）

**成功**

```json
{
  "error": 0,
  "uid": "10001",
  "token": "1f0c9d3a-....",
  "name": "norym",
  "has_pending": "0"
}
```

| 字段          | 类型   | 说明                                                                                                                                                |
|---------------|--------|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| `error`       | int    | `0` 成功，其余见 [`error-codes.md`](error-codes.md)                                                                                                 |
| `uid`         | string | 用户 id（字符串）                                                                                                                                   |
| `token`       | string | 校验通过的 token                                                                                                                                    |
| `name`        | string | 用户昵称（来自 MySQL）                                                                                                                              |
| `has_pending` | string | PEL 中是否有未 ACK 的聊天消息：`"1"` 有、`"0"` 无；为 `"1"` 时客户端应登录后立即发送 `1023` 拉取。为 `"0"` 也不代表没有新消息，建议登录后仍拉取一次 |

**失败**（仅含 `error`）

| error  | 含义                                                                                                                                   |
|--------|----------------------------------------------------------------------------------------------------------------------------------------|
| `0`    | 登录成功（此时才会携带 `uid` / `token` / `name`）                                                                                      |
| `1020` | uid 不是合法字符串（非字符串、空串或含非数字字符），未查询 Redis / MySQL                                                               |
| `1011` | StatusServer 中不存在该 uid 的 token，**或**登录后回查用户信息失败（数据库无该 uid / 数据库不可用）                                    |
| `1012` | token 与服务端记录不一致                                                                                                               |
| `1013` | 依赖服务不可用：Redis 注册 / 写在线状态失败，**或好友列表载入 Redis 失败**（见 [`api-chat.md`](api-chat.md#11-好友关系校验redis-set)） |
| `1002` | 调用 StatusServer 失败或超时                                                                                                           |

> 响应体由 JsonCpp 以 `toStyledString()` 序列化，是**带缩进的多行 JSON**；非 ASCII 字符可能以 `\uXXXX` 形式转义，客户端应按标准 JSON 解析而非按字节匹配。

### 2.3 MESSAGE_HEART_BEAT_REQ / RSP（1016 / 1017）

客户端登录后需**周期性发送心跳**（建议间隔 ≤15s）；服务端收到后把该用户在线 key `uip_<uid>` 的 TTL 续期为 **30s**。超过 30s 未续期，key 过期，该用户即被视为离线（他人申请加好友时不会再尝试投递）。

**请求（1016）**：body 可为空 JSON `{}`，内容不参与处理。会话尚未完成登录（未绑定 uid）时发送 → 回 `1011`。

**响应（1017）**

```json
{ "error": 0 }
```

| error | 含义 |
| ------- | ------ |
| `0` | 续期成功 |
| `1011` | 会话尚未登录（未绑定 uid），不允许续期 |
| `1013` | Redis 不可用，续期失败 |

## 3. 会话行为与限制

| 项 | 行为 |
| ---- | ------ |
| 读超时 | **无**（不完整报文会一直等待；已支持 `1016` 心跳续期在线状态，但尚未基于心跳做空闲回收 / 主动断开） |
| 头部校验 | `message_id > MAX_LENGTH` 或 `body_len > MAX_LENGTH` → 关闭会话 |
| 未知 message_id | 记录告警并丢弃该消息，不断开连接 |
| 发送队列 | 阈值 4；队列积压超过阈值说明对端不取数据 → **关闭会话**（不再静默丢包） |
| 写失败 | 丢弃剩余队列并关闭 socket，避免继续向坏连接投递 |
| 断开清理 | 读/写错误 → 关闭 socket、从会话表移除；已登录会话同时把本实例连接数 `-1` |
| 消息大小 | 单帧 body ≤ 2048 字节 |

## 4. 交互示例（登录）

```
客户端                                                     ChatServer
   |                                                            |
   |  connect 127.0.0.1:8090                                    |
   |----------------------------------------------------------->|
   |  [03 E9][00 23]{"uid":"10001","token":"1f0c...e9"}         |
   |----------------------------------------------------------->|
   |                                        StatusService.Login |
   |                                           （校验 uid+token） |
   |  [03 EA][00 14]{"error":0,"uid":10001,...}                 |
   |<-----------------------------------------------------------|
```

帧头说明：`03 E9` = message_id `1001`（`0x03E9`）；`00 23` = body 长度 35 字节（十进制）。

Python 参考实现：

```python
import json, socket, struct

payload = json.dumps({"uid": "10001", "token": "1f0c...e9"}).encode()
frame = struct.pack(">HH", 1001, len(payload)) + payload   # message_id=1001 (LOGIN_REQ)

s = socket.create_connection(("127.0.0.1", 8090), timeout=10)
s.sendall(frame)

head = s.recv(4)
message_id, body_len = struct.unpack(">hh", head)
body = b""
while len(body) < body_len:
    body += s.recv(body_len - len(body))
print(message_id, json.loads(body.decode()))
```

## 5. 已知限制（后续规划）

- 已实现 `1016` 心跳用于**在线状态续期**，但尚未基于心跳做**空闲超时回收**（长时间无心跳不会主动断开连接）；
- 已实现登录（含好友集合载入）、用户搜索、好友申请/同步/好友列表/删除好友/设置好友备注、编辑用户资料、聊天消息发送（含好友校验）/通知/拉取/确认（含
  PEL
  重投）、
  心跳； **历史记录与群聊尚未实现**；
- 明文传输（未启用 TLS），且无消息级鉴权（登录成功后不再校验后续帧）。

## 6. 聊天消息接口

聊天消息的发送 / 通知 / 拉取 / 确认收到 / Redis Stream 存储见独立文档 **[`api-chat.md`](api-chat.md)**。编号速查：

| message_id | 名称                             | 方向                    | 说明                 |
|------------|----------------------------------|-------------------------|----------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ`      | 客户端 → 服务端         | 发送消息             |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP`      | 服务端 → 客户端         | 发送回执             |
| `1022`     | `MESSAGE_NOTIFY_RECEIVE_MESSAGE` | 服务端 → 客户端（推送） | 有新消息（不含内容） |
| `1023`     | `MESSAGE_SYNC_CHAT_MESSAGE_REQ`  | 客户端 → 服务端         | 拉取消息             |
| `1024`     | `MESSAGE_SYNC_CHAT_MESSAGE_RSP`  | 服务端 → 客户端         | 拉取回包             |
| `1025`     | `MESSAGE_ACK_CHAT_MESSAGE_REQ`   | 客户端 → 服务端         | 确认收到消息         |
| `1026`     | `MESSAGE_ACK_CHAT_MESSAGE_RSP`   | 服务端 → 客户端         | 确认回包             |

> `MESSAGE_NOTIFY_CHAT_MSG_REQ`（`1015`）已定义但未使用。
>
> **好友校验**：发送 `1013` 前服务端会检查 Redis 好友集合 `userfriendset_<uid>`，非好友返回 `1014`（
> `error = 1022 NOT_FRIEND`），
> 消息不会写入离线消息流。集合的载入 / 续期 / 过期规则见 [`api-chat.md`](api-chat.md#11-好友关系校验redis-set)。

## 7. 好友与用户资料相关接口

好友申请 / 验证 / 在线查询等消息的完整字段、流程与错误码见独立文档 **[`api-friend.md`](api-friend.md)**；编辑用户资料见 [
`api-changes.md`](api-changes.md) §4。编号速查：

| message_id | 名称                               | 方向                    |
|------------|------------------------------------|-------------------------|
| `1005`     | `MESSAGE_ADD_FRIEND_REQ`           | 客户端 → 服务端         |
| `1006`     | `MESSAGE_ADD_FRIEND_RSP`           | 服务端 → 客户端         |
| `1007`     | `MESSAGE_SYNC_FRIEND_REQUEST_REQ`  | 客户端 → 服务端         |
| `1008`     | `MESSAGE_SYNC_FRIEND_REQUEST_RSP`  | 服务端 → 客户端         |
| `1009`     | `MESSAGE_NOTIFY_ADD_FRIEND_REQ`    | 服务端 → 客户端（推送） |
| `1010`     | `MESSAGE_AUTH_FRIEND_REQ`          | 客户端 → 服务端         |
| `1011`     | `MESSAGE_AUTH_FRIEND_RSP`          | 服务端 → 客户端         |
| `1012`     | `MESSAGE_NOTIFY_AUTH_FRIEND_REQ`   | 服务端 → 客户端（推送） |
| `1018`     | `MESSAGE_CHECK_IS_USER_ONLINE_REQ` | 客户端 → 服务端         |
| `1019`     | `MESSAGE_CHECK_IS_USER_ONLINE_RSP` | 服务端 → 客户端         |
| `1020`     | `MESSAGE_SYNC_FRIEND_LIST_REQ`     | 客户端 → 服务端         |
| `1021`     | `MESSAGE_SYNC_FRIEND_LIST_RSP`     | 服务端 → 客户端         |
| `1027`     | `MESSAGE_DEL_FRIEDN_REQ`           | 客户端 → 服务端         |
| `1028`     | `MESSAGE_DEL_FRIEDN_RSP`           | 服务端 → 客户端         |
| `1029`     | `MESSAGE_SET_FRIEND_REMARK_REQ`    | 客户端 → 服务端         |
| `1030`     | `MESSAGE_SET_FRIEND_REMARK_RSP`    | 服务端 → 客户端         |
| `1031`     | `MESSAGE_CHANGE_USER_INFO_REQ`     | 客户端 → 服务端         |
| `1032`     | `MESSAGE_CHANGE_USER_INFO_RSP`     | 服务端 → 客户端         |
