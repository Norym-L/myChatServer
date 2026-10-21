# 接口变更记录

> 按批次记录，最新批次在前。基线与日期以提交记录为准。

# 批次 2026-10-21（基线 `5524366`）：好友备注与用户资料编辑

## 1. 变更汇总

| 类别       | 内容                                                                                                           |
|------------|----------------------------------------------------------------------------------------------------------------|
| **新增**   | TCP `1029` / `1030` 设置好友备注；TCP `1031` / `1032` 编辑用户资料                                             |
| **变更**   | 用户资料缓存读取抽到 `RedisManager::getUserInfo`（`SearchUserByUid` 改为调用它，Redis 解析异常改为回落 MySQL） |
| **变更**   | 每次通过 `getUserInfo` 读取用户资料时，把 `ubaseinfo_<uid>` 的 TTL 续期为 **24 小时**（读即续期）              |
| **变更**   | 编辑资料成功后由 `RedisManager::setUserBaseInfo` 回写 `ubaseinfo_<uid>`，TTL **24 小时**，并保留 `token` 字段  |
| **变更**   | 用户名索引 `ubaseinfo_name_<name>` 的默认 TTL 由 `1800s` 调整为 **7 天**（`7 * 24 * 60 * 60`）                 |
| **未实现** | 备注 / 资料变更的推送通知；改名后旧名字索引的清理；头像等字段的内容校验                                        |

字段与流程详见本文 §3、§4，好友备注另见 [`api-friend.md`](api-friend.md) §10。

## 2. TCP 消息

| message_id | 名称                            | 方向            | 本批变化       |
|------------|---------------------------------|-----------------|----------------|
| `1029`     | `MESSAGE_SET_FRIEND_REMARK_REQ` | 客户端 → 服务端 | **新增枚举值** |
| `1030`     | `MESSAGE_SET_FRIEND_REMARK_RSP` | 服务端 → 客户端 | **新增枚举值** |
| `1031`     | `MESSAGE_CHANGE_USER_INFO_REQ`  | 客户端 → 服务端 | **新增枚举值** |
| `1032`     | `MESSAGE_CHANGE_USER_INFO_RSP`  | 服务端 → 客户端 | **新增枚举值** |

## 3. 设置好友备注（1029 / 1030）

### 3.1 请求

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

- 设置方取当前会话已登录 uid（`session->getUserId()`），请求体不需要传自己的 uid。
- 只修改 **当前用户视角**的单向备注（`user_friend.remark`），不会影响对方给本人设置的备注。

### 3.2 响应

```json
{ "error": 0, "id": "c-0001" }
```

| error  | 含义                                             | 存储过程 `update_friend_remark` 返回 |
|--------|--------------------------------------------------|--------------------------------------|
| `0`    | 设置成功                                         | `0`                                  |
| `1011` | 会话未登录（未绑定 uid）                         | ——                                   |
| `1020` | `friend_uid` 解析失败                            | ——                                   |
| `1022` | `NOT_FRIEND`：对方不是好友，或试图给自己设置备注 | `-1` / `-2`                          |
| `1013` | 数据库不可用                                     | 其它非 0 值（如 `-10`）              |

- 纯 MySQL 写操作， **不涉及 Redis**；成功后客户端需重新拉取好友列表（`1020`）才能看到新备注。
- 不通知被设置备注的好友（备注是单向的，对方无感知）。

## 4. 编辑用户资料（1031 / 1032）

### 4.1 请求

```json
{
  "id": "c-0002",
  "name": "新昵称",
  "desc": "新签名",
  "icon": "http://example.com/icon.png",
  "sex": 1
}
```

| 字段   | 类型   | 必填 | 说明                                                                                        |
|--------|--------|------|---------------------------------------------------------------------------------------------|
| `id`   | 任意   | 否   | 客户端消息标识，服务端**原样回显**                                                          |
| `name` | string | 否   | 昵称                                                                                        |
| `desc` | string | 否   | 个性签名                                                                                    |
| `icon` | string | 否   | 头像地址                                                                                    |
| `sex`  | int    | 否   | 性别，见 [`UserInfo.h`](../src/public/data/UserInfo.h) 的 `Sex`：`0` 男 / `1` 女 / `2` 未知 |

- **所有字段均可选**：字段缺失或显式传 `null` 表示不修改该项（`Json::Value::isNull()` 判定）。
- 全部字段缺失时仍会调用 Redis 与 MySQL（等价于一次空更新），返回值取决于存储过程，通常为 `0`。
- 修改对象固定为当前会话已登录 uid，请求体不传 uid。
- 字段长度、内容与 `sex` 取值范围均 **未校验**，由存储过程与数据库约束决定。

### 4.2 响应

```json
{ "error": 0, "id": "c-0002" }
```

| error  | 含义                                                          | 来源                                         |
|--------|---------------------------------------------------------------|----------------------------------------------|
| `0`    | 修改成功                                                      | Redis + MySQL 均成功                         |
| `1011` | 会话未登录，**或** MySQL 表示该 uid 不存在                    | 存储过程 `update_user_info` 返回 `-1`        |
| `1013` | 缓存中查不到该用户资料（未做前置查询），**或** MySQL 其它失败 | `changeUserInfo` 返回 `false`，或其它非 0 值 |

### 4.3 写入顺序与缓存行为

1. 先写 Redis：`RedisManager::changeUserInfo` → `getUserInfo`（读取并续期 24h）→ 覆盖请求中出现的字段 → `insert`（更新用户名索引
   `ubaseinfo_name_<name>`）+ `setUserBaseInfo`（覆盖 `ubaseinfo_<uid>`，TTL 24h）。
2. 再写 MySQL：`Mysql_boost::changeUserInfo` → `CALL update_user_info(uid, name, desc, icon, sex, @error)`。
3. 两步都成功才返回 `0`； **MySQL 失败不会回滚 Redis**。

- 修改前必然先查询：客户端在编辑资料前会查询用户资料，该查询经 `getUserInfo` 把 `ubaseinfo_<uid>` 写入或续期到 24h，因此第
  1 步实际能读到缓存。
- 资料缓存 `ubaseinfo_<uid>` 统一由 `setUserBaseInfo` 写入（TTL **24h**）：登录 / 搜索回源数据库后写一次，`getUserInfo`
  每次读取再续期 24h。
- 存储过程 `update_user_info` 定义不在本仓库，需由数据库侧提供；`@error` 返回 `0` 成功、`-1` 用户不存在。

## 5. 已知限制（本批新增）

1. **编辑资料依赖前置查询**：`RedisManager::changeUserInfo` 读不到 `ubaseinfo_<uid>` 时返回 `false`（`1013`
   ）且不写库。按约定客户端编辑前会先查询用户资料（查询会把缓存写入 / 续期），实际不会命中该分支；若跳过查询且缓存已过期，则仍会失败。
2. **缓存 TTL 统一为 24h**：登录 / 搜索回源数据库与编辑资料回写都走 `setUserBaseInfo`（24h），`getUserInfo` 每次读取再续期
   24h；只要用户 24h 内有过一次资料查询或登录，缓存就不会过期。
3. **Redis 故障时回包缺少 `error` 字段**：`getUserInfo` 的 `expire` 与 `setUserBaseInfo` 的 `SET` / `EXPIRE`
   都在异常捕获之外，Redis 不可用时会抛出，由 `dealMessage` 统一捕获；此时客户端收到只含 `id`、 **没有 `error` 字段**
   的回包（不会返回 `1013`），客户端需按缺省失败处理。登录路径由 `GetBaseInfo` 的 try 兜底（回落数据库），不受影响。
4. **改名不清理旧索引**：`ubaseinfo_name_<旧名>` 会保留至 TTL（7 天）到期，期间按旧用户名仍能搜到该用户。
5. **弱一致**：Redis 先写、MySQL 后写，MySQL 失败时缓存已变更，不回滚。
6. **无字段校验**：`name` / `desc` / `icon` 未做长度与内容过滤，`sex` 未校验是否属于 `0` / `1` / `2`；`remark`
   缺失即清空备注，请客户端显式传值。
7. **缓存内容解析失败不会回落 MySQL**：`getUserInfo` 只捕获异常，`Json::Reader::parse` 返回 `false` 时仍会用空
   `Json::Value` 构造结果（`uid = 0`、字段为空串）。此时 `SearchUserByUid` 返回的是这份空资料，而不是数据库数据。

## 6. 内部变更（不影响对外契约）

| 内容                                                                                                                                                                                                                                                                                                                    |
|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `src/public/const.h`：新增 `MESSAGE_SET_FRIEND_REMARK_REQ` / `_RSP`（`1029` / `1030`）与 `MESSAGE_CHANGE_USER_INFO_REQ` / `_RSP`（`1031` / `1032`）                                                                                                                                                                     |
| `src/chatServer/LogicSystem.h` / `.cpp`：新增 `setFriendRemarkhandler`、`changeUserInfoHandler` 并在 `registerCallBacks` 注册；`SearchUserByUid` 改为调用 `RedisManager::getUserInfo`                                                                                                                                   |
| `src/public/src/RedisManager_boost.*`：新增 `getUserInfo(uid)`（原 `SearchUserByUid` 中的缓存解析逻辑下沉，读取前把 key 续期 24h，保留 `token`，异常返回 `nullopt`）、`setUserBaseInfo(uid, info)`（写 `ubaseinfo_<uid>` 并设 TTL 24h）与 `changeUserInfo(...)`；`insert` 默认 TTL 改为 7 天；大量 `[[nodiscard]]` 标注 |
| `LogicSystem::GetBaseInfo` 改为只接收 `uid`：缓存读取走 `RedisManager::getUserInfo`，回源数据库后走 `setUserBaseInfo`；`setFriendRemarkhandler` 响应改用 `MESSAGE_SET_FRIEND_REMARK_RSP`（`1030`）                                                                                                                      |
| `src/public/sql/mysql/Mysql_boost.*`：新增协程版 `changeUserInfo`（`CALL update_user_info(?, ?, ?, ?, ?, @error)`，`std::optional` 参数直接绑定为 `NULL`）                                                                                                                                                              |
| `RedisManager_boost.h` 引入 `data/UserInfo.h`（对外暴露 `std::optional<UserInfo>`)                                                                                                                                                                                                                                      |

# 批次 2026-10-19（基线 `44ae18a`，提交于 2026-10-18）：聊天消息好友校验

## 1. 变更汇总

| 类别       | 内容                                                                                                                                                                                                                        |
|------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **新增**   | TCP `1027` / `1028` 删除好友；错误码 `1022 NOT_FRIEND`；Redis Set `userfriendset_<uid>` 好友关系缓存                                                                                                                        |
| **变更**   | 发送聊天消息（`1013`）前校验好友关系，非好友返回 `1022` 且不写入消息流；登录（`1001` / `1002`）时把好友列表载入 Redis（失败返回 `1013`）；接受好友申请（`1010` / `1011`）与删除好友（`1027` / `1028`）后同步 Redis 好友集合 |
| **未实现** | 删除好友通知被删除方；群聊；历史记录                                                                                                                                                                                        |

字段与流程详见 [`api-chat.md`](api-chat.md) §11、[`api-friend.md`](api-friend.md) §9。

## 2. TCP 消息

| message_id | 名称                        | 方向            | 本批变化                            |
|------------|-----------------------------|-----------------|-------------------------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ` | 客户端 → 服务端 | **新增好友校验**，非好友返回 `1022` |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP` | 服务端 → 客户端 | 新增错误码 `1022` 的返回            |
| `1027`     | `MESSAGE_DEL_FRIEDN_REQ`    | 客户端 → 服务端 | **新增枚举值**                      |
| `1028`     | `MESSAGE_DEL_FRIEDN_RSP`    | 服务端 → 客户端 | **新增枚举值**                      |

## 3. 发送聊天消息的好友校验（1013 / 1014）

请求与响应格式不变，仅在原有校验之后、写消息流之前增加好友判定：

```json
{ "msg_id": "c-0001", "to_uid": "10002", "type": "1", "body": "你好" }
```

```json
{ "error": 1022, "msg_id": "c-0001" }
```

| error  | 含义                                                  |
|--------|-------------------------------------------------------|
| `0`    | 已写入离线消息流（不代表对端已收到）                  |
| `1020` | `to_uid` / `type` 非法                                |
| `1022` | `NOT_FRIEND`：`to_uid` 不是当前用户的好友，消息未写入 |

- 判定方式：`SISMEMBER userfriendset_<from_uid> <to_uid>`，命中后顺带把 key 的 TTL 续期为 7 天。
- 非好友时 **不写 Stream、不推 `1022` 通知**，直接返回 `1014`。
- Redis 不可用导致 `SISMEMBER` 失败时同样返回 `1022`（无法区分"不是好友"与"缓存故障"）。

## 4. 删除好友（1027 / 1028）

请求：

```json
{ "uid": "10002" }
```

响应：

```json
{ "error": 0, "uid": "10002" }
```

| error  | 含义                             | 存储过程 `del_friend` 返回 |
|--------|----------------------------------|----------------------------|
| `0`    | 删除成功（MySQL + Redis 已同步） | `0`                        |
| `1020` | `uid` 解析失败                   | ——                         |
| `1022` | 好友关系不存在                   | `-1`                       |
| `1013` | 数据库 / Redis 不可用            | `-10` 及其它负值           |

- 删除者取当前会话已登录 uid，`uid` 只传被删除方；`uid` 为请求体原样回显。
- **双向删除**（由存储过程 `del_friend` 完成），成功后同步 Redis 好友集合。
- **不通知被删除方**，对端需重新拉取好友列表（`1020`）才能感知。

## 5. Redis 好友集合

| 项   | 值                                                                              |
|------|---------------------------------------------------------------------------------|
| key  | `userfriendset_<uid>`                                                           |
| 类型 | Set，成员为好友 uid 的十进制字符串                                              |
| TTL  | 7 天（`604800s`）滑动过期；校验命中 / 增删好友 / 首次载入时续期，登录本身不续期 |
| 命令 | `SISMEMBER`（校验）、`SADD`（载入 / 加好友）、`SREM`（删好友）、`EXPIRE`        |
| 载入 | 登录时若 key 不存在，从 MySQL `getUserFriendList` 全量载入，失败返回 `1013`     |

- 缓存不参与 MySQL 事务：数据库写成功而 Redis 写失败时只影响校验结果，不回滚数据库。
- **不活跃自动过期（设计行为）**：7 天滑动过期，只有好友校验命中（成功发消息）/ 增删好友 / 首次载入会续期，单纯登录不续期。
  连续 7 天不活跃的用户，其 `userfriendset_` 会自动从 Redis 释放；好友关系仍在 MySQL，下次登录重建。
- key 过期后下一次登录会从数据库重建，因此缓存过期不会丢失好友关系。

## 6. 内部变更（不影响对外契约）

| 内容                                                                                                                                                                                                  |
|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `src/public/const.h`：新增 `ErrorCode::NOT_FRIEND`（`1022`）与 `MessageType::MESSAGE_DEL_FRIEDN_REQ` / `MESSAGE_DEL_FRIEDN_RSP`（`1027` / `1028`）                                                    |
| `src/public/RedisManager.h`：新增好友集合前缀宏 `USER_FRIEND_SET`（`userfriendset_`）                                                                                                                 |
| `RedisManager` 新增 `isFriend` / `addFriend` / `delFriend` / `loadFriends`，以及底层 Set 命令封装 `sIsMember` / `sAdd` / `sRem`                                                                       |
| `LogicSystem::loginHandler`：登录成功后载入好友集合；`authFriendRequestHandler`：接受申请（`state = 1`）后写好友集合；`sendTextMessageHandler`：发送前校验好友；新增 `delFriendHandler` 并注册 `1027` |
| `RedisManager::loadFriends`：空好友列表直接返回成功，不写空 key                                                                                                                                       |
| `RedisManagerBoost` 去掉单例继承，改为空壳占位类（boost 协程版 Redis 接口待实现）                                                                                                                     |

## 7. 已知限制

1. **删除好友不通知被删除方**，`1028` 只回给发起方。
2. **Redis 异常被判为「不是好友」**：`SISMEMBER` 失败返回 `1022`，无法区分「不是好友」与「Redis 故障」；计划随 `RedisManager` 的
   boost 重构解决。
3. **弱一致**：登录只在 key 不存在时载入，缓存与数据库不一致时需等 7 天过期或手动删除 key；缓存过期后需重新登录才重建。

# 批次 2026-10-18（基线 `75d7459`）：聊天消息

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
- 未校验 `to_uid` 是否为好友（已在 2026-10-19 批次实现 Redis 好友校验）。
- Redis 写入失败只记日志，`1014` 仍返回 `0`。
- `body` / `msg_id` / `ids[]` 未做类型校验；单帧上限 2048 字节。
- 拉取为 **at-least-once**：未 ACK 的消息会被再次返回，客户端需按 `id` 去重；ACK（`XACKDEL`）后删除，无历史记录。
- `XACKDEL` 需要 Redis 8.2+，低版本上 ACK 会失败、消息会一直重投。
- 拉取 / 确认 **单次上限均为 10 条**（服务端硬编码），超过返回 `1020`。
