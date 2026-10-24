# 消息调度器与协程推送（实验）

> **状态：实验分支（仅 ChatServer）**。本批包含两处变化：
>
> 1. 提交 `de9c472`「消息调度器基础」：新增 `onlinechatmsgstream_<server_name>` 在线消息队列与 `MessageScheduler`；
> 2. 工作区未提交改动：`CSession` 协程发送接口 + **聊天消息统一走调度器推送**。
>
> 上一版列出的问题已全部修复（见 §8.1），编译错误清零；调度器推送已收敛到会话的 socket executor（见 §3.5），仅剩 4 个
> 稳定性注意项（见 §8.2）。验证步骤与通过标准见 §10。本文记录 **设计意图与已落地的代码结构**，不是当前主干行为的保证；
> 对外契约以 [`api-chat.md`](api-chat.md)、[`api-tcp.md`](api-tcp.md) 为准。

## 1. 背景

主干推送链路：

1. 发送方 `1013` → 好友 / 群成员校验 → `insertMessageToOffline` 写 `offlinestream_<to_uid>`；
2. `notifyMessageSend` 查 `uip_<to_uid>`：
    - 同实例：`UserManager::getSession` → `session->notifyReceiveMessage()` 推 `1022`；
    - 跨实例：gRPC `ChatService.NotifyChatMessage` → 对端实例推 `1022`；
3. 接收方收到 `1022` 后再发 `1023` 拉取正文，`1025` 确认。

痛点：

| 问题                   | 说明                                                           |
|------------------------|----------------------------------------------------------------|
| 两条推送路径           | 同实例直推与跨实例 gRPC 各写一遍，故障处理也各写一遍           |
| `1022` 只是"有新消息"  | 正文必须二次 `1023` 拉取，在线延迟多一个来回                   |
| 在线判定与投递有时间窗 | `uip_` 还在但会话可能已断开，推送失败无回传                    |
| 业务线程直接写 socket  | `send()` 与读 / 关闭共用 `m_sendMutex`，写失败只能丢弃整个队列 |

目标：把「投递」收敛到一个调度器；在线时直接把消息推给客户端， **推送失败再落离线流**；为后续把好友验证等非聊天消息
也纳入同一套推送做准备。

## 2. 变更一：消息调度器（`de9c472`）

### 2.1 组件

`MessageScheduler`（单例）在 `main` 中 `init(&server, logic.getIoContext())`，收发协程跑在 **逻辑线程**上，实际写 socket 由
会话的 executor 执行（见 §3.5）：

| 组件              | 数量              | 职责                                                                                                                                      |
|-------------------|-------------------|-------------------------------------------------------------------------------------------------------------------------------------------|
| `MessageReceiver` | 1 个协程          | `XREADGROUP` 阻塞读 `onlinechatmsgstream_<server_name>`，解析后写入 `MessageChannel`                                                      |
| `MessageChannel`  | 1 个，容量 `1000` | `channel<void(error_code, chat_message::Message)>`，既是队列也是背压点                                                                    |
| `MessageSender`   | 20 个协程         | 从 channel 取消息 → `XACKDEL` 在线流（标记已处理）→ `UserManager::getSession(uid)` → `co_spawn` 到会话 socket executor 推送；失败落离线流 |

读取参数：消费组 `message-receiver`、消费者 `receiver`、`COUNT 100`、`BLOCK 0`（永久阻塞）；`run()` 首次进入前用
`XGROUP CREATE ... MKSTREAM` 建组，起始 ID 为 `$`（`start_at = -1`，只消费建组之后的新消息），`BUSYGROUP` 视为已存在。

`MessageReceiver` 按字段还原消息：

| 字段                         | 解析                                       |
|------------------------------|--------------------------------------------|
| `to_uid`                     | → `Message::to_uid`（接收方 uid）          |
| `group_id`                   | → `Message::group_id`（群 id，单聊为 `0`） |
| `from_uid` / `type` / `body` | 同名字段；`message_id` 取 Stream 消息 ID   |

### 2.2 Redis 路由（`RedisManager::sendMessage`）

`chat_message::Message` 用 `group_id` 区分单聊 / 群聊，用 `group_checked` 标记"是否已完成分流"：

| 场景                                                                           | 行为                                                                                                                       |
|--------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
| 群聊（`group_id != 0`）                                                        | `SMEMBERS chatgroup_<gid>` → 复制消息、置 `group_checked = true`、`to_uid = 成员 uid`（保留 `group_id`）后**递归**调用自身 |
| 单聊首次进入（`group_id == 0`、`group_checked == false`）                      | 置 `group_checked = true` 后递归一次                                                                                       |
| 已完成分流（`group_checked == true`，`insertMessageToOffline` 只接受这种消息） | 对端在线（`uip_<to_uid>` 存在）→ `XADD onlinechatmsgstream_<对端实例>`；离线 → `XADD offlinestream_<to_uid>`               |

在线流字段：`group_id` / `to_uid` / `from_uid` / `type` / `body`（`group_id = 0` 表示单聊）。

- 在线流按 **实例名**分片，写在哪台实例的流上由 `uip_<to_uid>` 的值决定，因此发送方不需要知道对端实例的 gRPC 地址。
- 业务校验（好友 / 群成员）仍在 `sendTextMessageHandler`，调度器只负责投递，不重复鉴权。

### 2.3 与旧路径的关系

- `notifyMessageSend`（`1022` 通知 + gRPC 转发）被弃用：业务处理器不再主动通知，消息进入在线流后由调度器直推。
- `ChatServiceImpl::NotifyChatMessage` 暂未删除，属于待清理的旧路径。

## 3. 变更二：Session 协程发送接口（工作区）

### 3.1 旧接口

```cpp
void CSession::send(const char* message, size_t len, short message_id);
```

`m_sendMutex` + `m_sendQueue` + `async_write` 回调链：调用方拿不到结果，队列超过 `MAX_SEND_QUEUE_LEN`（4）直接关闭会话。

### 3.2 新接口

| 名称                 | 签名 / 类型                                                             | 说明                                              |
|----------------------|-------------------------------------------------------------------------|---------------------------------------------------|
| `pushMessage`        | `awaitable<bool> pushMessage(std::string body, std::uint16_t id) const` | 协程发送，返回是否真正写成功                      |
| `writeLoop`          | `awaitable<void> writeLoop()`                                           | `start()` 时 `co_spawn`，串行消费 `m_sendChannel` |
| `SendMessageNode`    | `{ body, result, message_id }`                                          | 待发送节点，`result` 为容量 1 的结果通道          |
| `SendResultChannel`  | `channel<void(error_code)>`                                             | 写结果回传                                        |
| `SendMessageChannel` | `channel<void(error_code, shared_ptr<SendMessageNode>)>`                | 每会话的发送队列                                  |

`pushMessage` 流程：

1. 组装 `SendMessageNode`，`result` 是新建的 `SendResultChannel`；
2. `m_sendChannel->async_send(...)` 入队；channel 满时协程挂起（背压，不丢包也不关连接）；
3. `writeLoop` 取出节点 → `SendNode` 组帧（4 字节头）→ `net::async_write`；
4. 无论成功失败，把 `error_code` 回写 `result`；写失败后 `writeLoop` 退出；
5. `pushMessage` 在 `result` 上 `async_receive`，返回 `!ec`；
6. channel 已关闭（`writeLoop` 退出后）时 `async_send` 立即失败，`pushMessage` 直接返回 `false`。

由此： **发送结果第一次能回传给业务协程**；同一会话的写顺序由 channel FIFO + 单个 `writeLoop` 保证。

### 3.3 推送报文（`1047`）

`CSession::sendMessage(chat_message::Message)` 改为协程并返回 `bool`，序列化后以 `MESSAGE_CHAT_MESSAGE` 帧推送：

```json
{
  "group_id": "0",
  "from_uid": "10002",
  "type": 1,
  "body": "你好",
  "time": "1734567890123-0"
}
```

| 字段       | 说明                                                                              |
|------------|-----------------------------------------------------------------------------------|
| `group_id` | 群 id 的十进制字符串；**单聊为 `"0"`，字段始终出现**（原布尔字段 `group` 已取消） |
| `from_uid` | 发送方 uid 的十进制字符串                                                         |
| `type`     | `1` 文本 / `2` 图片 / `3` 文件 / `4` 表情包                                       |
| `body`     | 消息内容                                                                          |
| `time`     | Redis Stream 生成的消息 ID（毫秒时间戳-序号），可作排序 / 去重键                  |

帧号：`MessageType::MESSAGE_CHAT_MESSAGE = 1047`， **不是** `1022`。客户端需要新增该帧的处理。

### 3.4 投递与在线流确认（XACKDEL 的语义）

`MessageSender::run()`：

```text
取消息 → XACKDEL 在线流 → UserManager::getSession(to_uid) → sendMessage(会话)
        → 返回 false 时 insertMessageToOffline
```

- `XACKDEL` 的含义是"该消息 **已被调度器取出处理**"，用于删除在线流里的分发副本， **不是**客户端收件确认；客户端确认收件
  仍走 `1025` / `offlinestream_<uid>` 的 ACK（`ackAndDelChatMessages`）。
- 因此确认发生在投递之前：消息不会在在线流中重复分发；未送达的消息统一由 `insertMessageToOffline` 写入
  `offlinestream_<uid>` 兜底，等客户端 `1023` 拉取。
- 返回 `false` 覆盖三种情况：本实例没有该 uid 的会话、发送 channel 已关闭、socket 写失败。
- 在线流只读 `>`（新消息）， **不做 PEL 重投**；在线流的清理完全依赖 `XACKDEL`，没有"未 ACK 就重新投递"的保障。
- 已知边界（分发语义下的取舍）：
    - `XACKDEL` 成功、`insertMessageToOffline` 之前进程崩溃 → 该条消息丢失（在线流已删、离线流未写）；
    - `insertMessageToOffline` 内部 `catch` 掉 Redis 异常只记日志，写失败不会回滚 `XACKDEL`；
    - 若要求"绝不丢"，需要把确认放到落库成功之后，代价是崩溃时可能重复投递。

### 3.5 线程模型与 executor 约定

当前 ChatServer 有四个执行单元：

| 执行单元                   | 承载内容                                                          | 线程数                |
|----------------------------|-------------------------------------------------------------------|-----------------------|
| main `io_context`          | acceptor、signal                                                  | 1                     |
| `AsioIOServicePool`        | 每个 session 的 socket / `readLoop` / `writeLoop`                 | 4 个 context × 1 线程 |
| `LogicSystem::m_ioContext` | 100 个 `dealMessage` 协程、Redis、MySQL、`MessageScheduler`、心跳 | 4                     |
| gRPC server                | 同步 `ChatService`（`ChatServiceImpl`）                           | 1（独立线程）         |

`m_sendChannel` 的线程收敛（本批第一步）：

```cpp
// MessageScheduler.cpp：把推送切到会话 socket 的 executor
co_await net::co_spawn(session->getSocket().get_executor(),
                       session->sendMessage(message),
                       net::use_awaitable);
```

- `MessageSender` 在逻辑线程取到消息，但 `sendMessage` / `pushMessage` 的实际执行被派发到该 session 的 socket
  executor；`m_sendChannel` 的 `async_send`、`writeLoop` 的 `async_receive`、`close()` 里的 `close(channel)` 因此都在 **同一个
  io_context**（pool 中每条 context 只有一条线程）上执行。
- `pushMessage` 里的 `SendResultChannel` 用 `co_await net::this_coro::executor` 构造，也落在同一 executor，写结果回传不再跨线程。
- 结论：当前不需要为 channel 加锁或 strand； **前提是 `AsioIOServicePool` 每条 context 保持单线程**。若将来改成多线程 run
  同一个 context，必须给每个 session 套 `make_strand`，否则 channel 与 socket 的并发假设全部失效。

**未采纳：把所有 io_context 合并进 main**。原因：

1. 业务协程里有同步 gRPC 调用（`ChatGrpcClient`，超时 3s，见 `LogicSystem.cpp` 的 `GrpcSendFriendRequestToOther` /
   `GrpcAuthFriendRequestAndNotifyToOther` / `GrpcNotifyReceiveMessageToOther`），合并成单线程后一次跨服调用会阻塞全部连接；
2. gRPC 服务用 `RedisManager::wait()` 同步等 Redis，该接口禁止在承载 Redis 连接的线程上调用，gRPC 线程必须独立；
3. 合并且多线程 run 时所有 session 需要 strand 化，改动面比收益大。

## 4. 业务层改动（工作区）

| 位置                              | 改动                                                                                                                                                               |
|-----------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `sendTextMessageHandler`          | `insertMessageToOffline` → `RedisManager::sendMessage`；请求字段映射为 `to_uid = 群聊 ? 0 : 目标 uid`、`group_id = 群聊 ? 群 id : 0`；`notifyMessageSend` 调用注释 |
| `sendTextMessageHandler` 群聊校验 | 不在群内的错误码由 `1022 NOT_FRIEND` 改为 `1023 GROUP_ID_INVALID`                                                                                                  |
| `syncChatMessageHandler`          | `1024` 的 `msgs[]` 改为 `group_id` / `from_uid` / `type` / `body` / `time`（取消布尔 `group`，`id` 改名 `time`）                                                   |
| 群创建 / 拉人 / 踢人 / 解散       | 系统消息仍用 `RedisManager::sendMessage`，`notifyMessageSend` 全部注释，避免"通知 + 直推"重复                                                                      |
| `notifyMessageSend`               | 弃用标记（`static_assert`）已删除，函数与调用点保留待清理                                                                                                          |
| 好友验证推送                      | `sendAuthFriendRequest` / `sendFriendRequestResult` 仍走旧 `send()`，后续若实验通过再迁移                                                                          |

## 5. 时序

**单聊在线（同实例）**

```text
发送方 1013 → 逻辑线程：好友校验 → RedisManager::sendMessage（group_checked=false）
            → group_checked=true 再递归一次 → XADD onlinechatmsgstream_<本实例>
调度器 receiver → XREADGROUP → MessageChannel → sender
            → XACKDEL 在线流 → UserManager::getSession(uid)
            → CSession::sendMessage → pushMessage → m_sendChannel → writeLoop
            → async_write(1047) → 客户端；写失败则 XADD offlinestream
发送方同步收到 1014（仅代表已交给调度器）
```

**跨实例**：发送方所在实例仍查 `uip_<to_uid>`，但只把消息写进 **对端实例**的 `onlinechatmsgstream_<name>`，无需 gRPC 调用。

**离线**：`sendMessage` 直接写 `offlinestream_<to_uid>`，不进入在线流；客户端上线后走 `1023` / `1024` / `1025`（链路不变，
`msgs[]` 字段见 §4）。

**群聊**：`sendMessage` 按 `chatgroup_<gid>` 递归扇出到每个成员（置 `group_checked = true`、`to_uid = 成员 uid`，
保留 `group_id`）；在线成员收 `1047`，离线成员各自入 `offlinestream_<uid>`。

## 6. 兼容性

| 场景                | 影响                                                                                                          |
|---------------------|---------------------------------------------------------------------------------------------------------------|
| 新服务端 + 旧客户端 | 旧客户端不认识 `1047`；且在线消息**不再写入** `offlinestream_`，旧客户端收不到正文 → 实验期不能混跑           |
| 新客户端 + 旧服务端 | 只收 `1022` 时仍按原逻辑 `1023` 拉取，可兼容                                                                  |
| 新旧服务端混跑      | 旧实例写 `offlinestream` + gRPC 通知，新实例写在线流；同一用户被分到不同实例时行为不一致 → 建议整集群一起切换 |
| 离线消息            | 链路未变（`1023` / `1024` / `1025`），但 `msgs[]` 字段随本批调整：`group` 取消、`id` → `time`                 |

客户端建议：同时处理 `1047`（直接用正文）与 `1022`（保留旧的拉取逻辑），并按 `time` 去重。

## 7. 键位与参数

| 键 / 参数                           | 值                                                                                                                   |
|-------------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `onlinechatmsgstream_<server_name>` | 每实例一个在线消息流，由 `RedisManager::sendMessage` 写入；字段 `group_id` / `to_uid` / `from_uid` / `type` / `body` |
| 消费组 / 消费者                     | `message-receiver` / `receiver`（`run()` 内 `XGROUP CREATE ... MKSTREAM`，起始 ID `$`）                              |
| 读取                                | `XREADGROUP GROUP message-receiver receiver COUNT 100 BLOCK 0 STREAMS <stream> >`                                    |
| 确认                                | `XACKDEL <stream> message-receiver ACKED IDS <n> <id>`：表示"已被调度器取出处理"，不是客户端收件确认（见 §3.4）      |
| `offlinestream_<uid>`               | 离线消息流；字段同上（`group_id` / `to_uid` / `from_uid` / `type` / `body`），由 `insertMessageToOffline` 写入       |
| `uip_<uid>`                         | 在线实例名，决定消息写入哪条在线流                                                                                   |

## 8. 当前状态

### 8.1 已修复

| #  | 原问题                                                              | 现状                                                                                                                                                                           |
|----|---------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1  | `m_sendChannel` 未初始化                                            | 构造函数中创建 `SendMessageChannel`（容量 10）                                                                                                                                 |
| 2  | `CServer::getSession` 用 uid 查以 `sessionId` 为 key 的 map         | 声明与 `CServer.cpp` 中的定义均已删除，统一走 `UserManager::getSession(uid)`                                                                                                   |
| 3  | 在线流缺 `to_uid`，`MessageReceiver` 只从 `group_id` 还原           | 在线流写 `group_id` / `to_uid` / `from_uid` / `type` / `body`，接收端分别解析                                                                                                  |
| 4  | 消费组未创建，`XREADGROUP` 抛 `NOGROUP` 后死循环                    | `MessageReceiver::run` 先 `XGROUP CREATE ... MKSTREAM`（起始 ID `$`，`BUSYGROUP` 视为成功）                                                                                    |
| 5  | `notifyMessageSend` 的非法 `static_assert`                          | 已删除                                                                                                                                                                         |
| 6  | `XACKDEL` 组名写成消费者名                                          | `MessageSender` 增加 `m_groupName`（`message-receiver`）                                                                                                                       |
| 7  | 群聊扇出漏改 `is_to_group`                                          | `Message` 改为 `group_id` + `group_checked`，扇出时置 `group_checked = true`、`to_uid = 成员 uid`                                                                              |
| 8  | 推送报文/回包用布尔 `group`                                         | `1047` 与 `1024` 统一改为 `group_id`（单聊 `"0"`），`id` 改名 `time`                                                                                                           |
| 9  | `sendMessage` 缺收尾 `co_return` 导致重复递归                       | 已分流分支末尾 `co_return`                                                                                                                                                     |
| 10 | 调度器未判空 `UserManager::getSession`                              | 已改为 `if (!session \|\| !co_await session->sendMessage(...))`                                                                                                                |
| 11 | `getMessageFromOffline` 仍用 `is_to_group`、把群 id 写进 `to_uid`   | 改为解析 `group_id` / `to_uid` / `from_uid` / `type` / `body`                                                                                                                  |
| 12 | `close()` 不关闭发送 channel，`writeLoop` 永久挂起                  | `close()` 中先 `m_sendChannel->close()` 再关 socket                                                                                                                            |
| 13 | `insertMessageToOffline` 分支条件反转，发送时对端离线的消息写不进流 | 改为 `if (message.group_checked)` 落单聊离线流，未分流消息只打告警；群聊落库分支删除                                                                                           |
| 14 | `XGROUP CREATE` 从 `0` 开始消费，会重放残留消息                     | 传 `start_at = -1`（`$`）                                                                                                                                                      |
| 15 | 调度器在逻辑线程直接调用 `pushMessage`，channel 跨线程操作          | 调度器改为 `co_spawn(session->getSocket().get_executor(), ...)`；`send()` 也 co_spawn 到同一 executor，channel 的 send / receive / close 收敛到同一 io_context 线程（见 §3.5） |

### 8.2 仍存在的问题

| # | 问题                                                                                                                           | 位置                   |
|---|--------------------------------------------------------------------------------------------------------------------------------|------------------------|
| 1 | `MessageReceiver` 直接 `stream_results.front()`，空结果（超时 / 空响应）会越界                                                 | `MessageScheduler.cpp` |
| 2 | 旧 `send()` 的 `m_sendQueue` / `m_sendMutex` / `handleWrite` / `MAX_SEND_QUEUE_LEN` 已成死代码，旧背压（队列满踢连接）不再生效 | `CSession.cpp`         |
| 3 | 在线流无 TTL，只靠 `XACKDEL` 删除；Redis 低于 8.2 时确认失败，流持续增长                                                       | `MessageSender::run`   |
| 4 | `streamReadGroup` 形参拼写为 `bolck`（不影响调用，建议改名）                                                                   | `RedisManager_boost.h` |

## 9. 后续计划（实验通过后）

1. 修完 §8.2，先在单实例跑通单聊 / 群聊 / 离线三条路径。
2. 把好友验证、好友申请结果等推送迁移到 `MessageScheduler`：`Message` 增加消息类别字段，调度器按类别选择帧号。
3. 删除 `notifyMessageSend`、gRPC `NotifyChatMessage` 与相关 proto 消息。
4. 统一会话写路径：全部走 `pushMessage`，删除 `send()` / `handleWrite` / `m_sendQueue`。
5. 增加监控：在线流长度、待确认条数、`MessageChannel` 深度、推送失败率。

## 10. 可行性验证方案

### 10.1 前置条件

- 先单实例（`ChatServer.exe --config=chat1.ini`），通过后再加 `chat2` 验证跨实例。
- Redis ≥ 8.2（`XACKDEL`），否则在线流不会被删除，`XLEN` 会一直增长。
- 客户端必须支持 `1047`；旧客户端不参与（见 §6）。
- 日志级别调到 debug，便于观察 `push failed` / `有消息未检查`。

### 10.2 观测手段

| 命令                                                   | 期望                                                      |
|--------------------------------------------------------|-----------------------------------------------------------|
| `XLEN onlinechatmsgstream_<name>`                      | 稳态接近 `0`，不随时间单调增长                            |
| `XINFO GROUPS onlinechatmsgstream_<name>`              | 组名 `message-receiver`、`consumers >= 1`、`pending == 0` |
| `XPENDING onlinechatmsgstream_<name> message-receiver` | `0`（`XACKDEL` 生效）                                     |
| `XLEN offlinestream_<uid>`                             | 对端离线时 `+1`；客户端 `1025` 确认后归 `0`               |
| `GET uip_<uid>`                                        | 值为当前实例名，用于判断消息该写哪条在线流                |

日志关键字：正常时 **不应**出现 `[message-receiver] broken`、`有消息未检查`、`push exception:`；`push failed, drop one
message` 只应出现在会话刚断开的竞争窗口内。

### 10.3 用例矩阵

| #  | 场景         | 操作                                  | 期望                                                                                                 | 观测点                                           |
|----|--------------|---------------------------------------|------------------------------------------------------------------------------------------------------|--------------------------------------------------|
| 1  | 同实例单聊   | A、B 连同一实例，A 发 1 条            | B 收到 `1047`，`group_id="0"`、`time` 为 Stream ID                                                   | `XLEN online...` 回到 0                          |
| 2  | 跨实例单聊   | A 在 chat1、B 在 chat2，A 发 1 条     | 消息只写 `onlinechatmsgstream_chat2`；B 收到 `1047`；无 gRPC 调用                                    | 两条在线流 `XLEN`                                |
| 3  | 接收方刚断线 | B 杀进程（`uip_` 未过期），A 发 1 条  | 推送返回 `false` → 写 `offlinestream_<B>`；B 重连后 `1023` 拉到                                      | `XLEN offlinestream_<B>`                         |
| 4  | 接收方离线   | B 正常下线等 `uip_` 过期，A 发 1 条   | 直接入 `offlinestream_<B>`，不进在线流                                                               | 在线流 `XLEN` 不变                               |
| 5  | 群聊跨实例   | 群成员分布在 chat1/chat2，群内发 1 条 | 每名成员按各自 `uip_` 收到 `1047` 或落离线流；系统消息同样投递                                       | 各成员 `offlinestream` 长度                      |
| 6  | 快速连发     | A 连续发 50 条（不停顿）              | B 收到的 `1047` 顺序与发送一致，`time` 严格递增                                                      | 客户端按 `time` 校验                             |
| 7  | 慢客户端     | B 暂停读 socket，A 连续发 100 条      | channel 满后 `pushMessage` 挂起（背压），不丢消息、不关连接；恢复读后全部到达                        | 服务端无异常日志                                 |
| 8  | Redis 重启   | 运行中重启 Redis                      | `MessageReceiver` 重建消费组并继续消费；在线消息恢复投递                                             | `[message-receiver] broken` 后应恢复，不无限刷屏 |
| 9  | 离线拉取     | 场景 3/4 后 B 上线，`1023` → `1025`   | `msgs[]` 含 `group_id` / `from_uid` / `type` / `body` / `time`；确认后 `XLEN offlinestream_<B>` 归 0 | `XPENDING offlinestream_<B>`                     |
| 10 | 旧客户端     | 不支持的客户端连入                    | 预期**失败**（收不到 `1047`），确认 §6 的兼容性判断成立                                              | ——                                               |

### 10.4 通过标准

| 维度   | 标准                                                                                                             |
|--------|------------------------------------------------------------------------------------------------------------------|
| 功能   | 用例 1–5、9 全部符合期望；`1047` 字段完整、`time` 可排序去重                                                     |
| 一致性 | 在线流 `XLEN` 稳态接近 0、`XPENDING == 0`；按 `time` 去重后重复率 0；无消息丢失（发送条数 == 到达 + 离线流条数） |
| 稳定性 | 连续 30 分钟、每秒 10 条以上压测，无崩溃、无内存持续增长、无 `[message-receiver] broken` 刷屏、无 `有消息未检查` |
| 背压   | 用例 7 不丢消息，进程 CPU/内存平稳                                                                               |

任一以下情况出现即判定 **不可行**并回滚：在线流 `XLEN` 持续增长、`XPENDING` 长期非 0、出现消息丢失、进程异常退出、`broken`
日志持续刷屏。

### 10.5 回滚

1. 业务层：恢复 `sendTextMessageHandler` 与群操作中被注释的 `notifyMessageSend`，把 `RedisManager::sendMessage` 改回
   `insertMessageToOffline`（参考 `de9c472` 前的实现）。
2. `CSession::sendMessage` 改回同步 `send()`（或保留 §3.2 的协程接口但由业务层直接 await）。
3. 客户端切回 `1022` 通知 + `1023` 拉取。
4. 清理残留：`DEL onlinechatmsgstream_<name>`。

### 10.6 与"合并 io_context"的关系

本方案按现有四类执行单元验证，不依赖第二步（合并 io_context / strand 化）。若压测中出现以下现象，再考虑第二步：

- 单实例 CPU 未打满但吞吐上不去（逻辑线程 4 条互相争用 `concurrent_channel`）；
- 跨线程唤醒成为热点（`pushMessage` 的 executor 切换在 profile 中占比高）。
