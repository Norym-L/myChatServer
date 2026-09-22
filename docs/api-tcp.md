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
|------|----|------|
| `HEAD_TOTAL_LEN` | 4 | 头部总长（2 + 2） |
| `HEAD_ID_LEN` | 2 | message_id 长度 |
| `HEAD_DATA_LEN` | 2 | body_len 长度 |
| `MAX_LENGTH` | 2048 | body 最大长度 |

> ⚠️ **兼容性变更**：历史版本的服务端响应**只发送 body、不写 4 字节帧头**，任何按协议解析的客户端都无法工作。当前实现已按上表写入完整帧头；若客户端此前是"裸读 JSON"，需同步改为先读 4 字节头再读 body。

## 2. 消息定义

| message_id | 名称 | 方向 | body |
|-----------|------|------|------|
| `0` | `MESSAGE_CHAT_LOGIN` | 客户端 → 服务端 | JSON `{ "uid": <int>, "token": <string> }` |
| `1` | `MESSAGE_CHAT_LOGIN_RESP` | 服务端 → 客户端 | JSON，见下表 |

### 2.1 MESSAGE_CHAT_LOGIN（0）

```json
{ "uid": 10001, "token": "1f0c9d3a-...." }
```

- `uid` / `token` 取自 `POST /user_login` 的响应。
- 服务端将 `uid` 与 `token` 提交给 StatusServer 的 `StatusService.Login` 校验。

### 2.2 MESSAGE_CHAT_LOGIN_RESP（1）

**成功**

```json
{ "error": 0, "uid": 10001, "token": "1f0c9d3a-....", "name": "norym" }
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` 成功，其余见 [`error-codes.md`](error-codes.md) |
| `uid` | int | 用户 id |
| `token` | string | 校验通过的 token |
| `name` | string | 用户昵称（来自 MySQL） |

**失败**（仅含 `error`）

| error | 含义 |
|-------|------|
| `0` | 登录成功（此时才会携带 `uid` / `token` / `name`） |
| `1011` | uid 校验失败：StatusServer 中不存在该 uid 的 token，**或**登录后回查用户信息失败（数据库无该 uid / 数据库不可用） |
| `1012` | token 与服务端记录不一致 |
| `1002` | 调用 StatusServer 失败或超时 |

> 响应体由 JsonCpp 以 `toStyledString()` 序列化，是**带缩进的多行 JSON**；非 ASCII 字符可能以 `\uXXXX` 形式转义，客户端应按标准 JSON 解析而非按字节匹配。

## 3. 会话行为与限制

| 项 | 行为 |
|----|------|
| 读超时 | **无**（不完整报文会一直等待，未实现心跳/空闲回收） |
| 头部校验 | `message_id > MAX_LENGTH` 或 `body_len > MAX_LENGTH` → 关闭会话 |
| 未知 message_id | 记录告警并丢弃该消息，不断开连接 |
| 发送队列 | 阈值 4；队列积压超过阈值说明对端不取数据 → **关闭会话**（不再静默丢包） |
| 写失败 | 丢弃剩余队列并关闭 socket，避免继续向坏连接投递 |
| 断开清理 | 读/写错误 → 关闭 socket 并从会话表移除 |
| 消息大小 | 单帧 body ≤ 2048 字节 |

## 4. 交互示例（登录）

```
客户端                                                     ChatServer
   |                                                            |
   |  connect 127.0.0.1:8090                                    |
   |----------------------------------------------------------->|
   |  [00 00][00 23]{"uid":10001,"token":"1f0c...e9"}           |
   |----------------------------------------------------------->|
   |                                        StatusService.Login |
   |                                           （校验 uid+token） |
   |  [00 01][00 14]{"error":0,"uid":10001,...}                 |
   |<-----------------------------------------------------------|
```

帧头说明：`00 00` = message_id 0；`00 23` = body 长度 35 字节（十进制）。

Python 参考实现：

```python
import json, socket, struct

payload = json.dumps({"uid": 10001, "token": "1f0c...e9"}).encode()
frame = struct.pack(">hh", 0, len(payload)) + payload   # message_id=0 (LOGIN)

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

- 未实现心跳/空闲超时，粘性连接会长期占用会话；
- 仅支持登录消息，尚无聊天转发/群组等业务消息类型；
- 明文传输（未启用 TLS），且无消息级鉴权（登录成功后不再校验后续帧）。
