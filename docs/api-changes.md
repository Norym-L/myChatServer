# 接口变更记录

> 按批次记录，最新批次在前。基线与日期以提交记录为准。

# 批次 2026-10-24（基线 `de9c472` + 工作区未提交）：消息调度器与协程推送（实验）

> **实验性质，当前不可运行**。设计、时序、Redis 键位与已知问题见 [`design-message-push.md`](design-message-push.md)。

## 1. 变更汇总

| 类别       | 内容                                                                                                                                                                 |
|------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **新增**   | Redis Stream `onlinechatmsgstream_<server_name>`：每实例一个在线消息流，作为所有聊天消息的统一投递入口                                                               |
| **新增**   | `MessageScheduler`：1 个 `XREADGROUP` 阻塞消费者 + 20 个投递协程 + 容量 1000 的 `MessageChannel`                                                                     |
| **新增**   | `RedisManager::sendMessage`：单聊在线写在线流、离线写 `offlinestream_<uid>`、群聊按 `chatgroup_<gid>` 成员逐个扇出                                                   |
| **新增**   | `CSession::pushMessage` / `writeLoop`：协程发送接口，返回是否真正写完，替代 `send()` 的"无结果"队列                                                                  |
| **新增**   | TCP `1047 MESSAGE_CHAT_MESSAGE`：调度器直推**完整消息体**（`group_id` / `from_uid` / `type` / `body` / `time`）                                                      |
| **变更**   | `1013` 不再调用 `notifyMessageSend`；消息交给调度器，在线由调度器直推、离线才落 `offlinestream`                                                                      |
| **变更**   | 群创建 / 拉人 / 踢人 / 解散的系统消息同样只走调度器，`notifyMessageSend` 调用全部注释                                                                                |
| **变更**   | 群聊发送时"不在群"的错误码由 `1022 NOT_FRIEND` 改为 `1023 GROUP_ID_INVALID`                                                                                          |
| **变更**   | `1024` 的 `msgs[]` 字段调整：取消布尔 `group`，`group_id` 改为恒出现（单聊 `"0"`），`id` 改名 `time`（仍是 ACK 用的 Stream ID）                                      |
| **变更**   | 内部 `chat_message::Message`：`is_to_group` 拆为 `group_id` + `group_checked`（发送方 / 分流阶段用）                                                                 |
| **废弃**   | `notifyMessageSend`（`1022` + gRPC 转发）；gRPC `ChatService.NotifyChatMessage` 暂未删除                                                                             |
| **说明**   | 在线流的 `XACKDEL` 含义是"调度器已取出处理"，**不是**客户端收件确认；未送达的消息统一由 `insertMessageToOffline` 写入 `offlinestream_<uid>` 兜底                     |
| **说明**   | 调度器推送改为 `co_spawn` 到会话的 socket executor，`m_sendChannel` 的 send / receive / close 收敛到同一 io_context 线程；暂不合并 io_context（原因见设计文档 §3.5） |
| **未完成** | 编译错误已清零，链路已通；剩余 4 个稳定性注意项（`front()` 无空检查、旧发送队列死代码导致背压失效、在线流无 TTL、参数拼写），见设计文档 §8.2                         |

## 2. TCP 消息

| message_id | 名称                             | 方向                    | 本批变化                                     |
|------------|----------------------------------|-------------------------|----------------------------------------------|
| `1047`     | `MESSAGE_CHAT_MESSAGE`           | 服务端 → 客户端（推送） | **新增枚举值**                               |
| `1022`     | `MESSAGE_NOTIFY_RECEIVE_MESSAGE` | 服务端 → 客户端         | 聊天消息不再使用；好友验证等未迁移的推送仍用 |

## 3. 推送报文体（1047）

```json
{
  "group_id": "0",
  "from_uid": "10002",
  "type": 1,
  "body": "你好",
  "time": "1734567890123-0"
}
```

- `group_id` 恒出现，单聊为 `"0"`、群聊为群 id（原布尔 `group` 字段取消）；`time` 为 Redis Stream 消息 ID，客户端可用于排序与去重。
- 在线消息 **不再写入** `offlinestream_<uid>`；推送失败（会话不存在 / 写失败）才回落到离线流。
- 旧客户端不认识 `1047`，且不会再从 `1023` 拉到在线消息 → **实验期不能与旧客户端混跑**。

## 4. 内部变更（不影响对外契约）

| 内容                                                                                                                                                                                                                                                                                                                                                                                                |
|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 新增 `src/chatServer/MessageScheduler.*`：`MessageReceiver` / `MessageSender` / `MessageScheduler`，继承 `RedisManager` 以复用受保护的构造与 `init`                                                                                                                                                                                                                                                 |
| `src/chatServer/CServer.*`：删除 `getSession(uid)` 的声明与定义（uid → session 统一走 `UserManager`）                                                                                                                                                                                                                                                                                               |
| `src/chatServer/net/CSession.*`：构造函数初始化 `SendMessageChannel`（容量 10）；新增 `SendMessageNode` / `SendResultChannel`、`writeLoop`、`pushMessage`；`sendMessage(Message)` 改为协程返回 `bool`，推送 `1047`；`close()` 关闭发送 channel；`SendNode` 的 `message_id` 改为 `std::uint16_t`                                                                                                     |
| `src/public/MessageType.h`：`Message` 用 `group_id`（单聊 0）与 `group_checked`（分流标记）替代 `is_to_group`                                                                                                                                                                                                                                                                                       |
| `src/public/src/RedisManager_boost.*`：新增 `ONLINE_MESSAGE_STREAM` 宏、`sendMessage`（`group_id` 分流 + `group_checked` 标记）；在线 / 离线流统一写 `group_id` / `to_uid` / `from_uid` / `type` / `body`；`insertMessageToOffline` 只接受已分流消息（`group_checked == true`），未分流消息只打告警；`streamReadGroup` 增加 `BLOCK` 参数；构造函数 / 析构 / 连接成员改为 `protected` 以便调度器继承 |
| `main.cpp`：构造并启动 `MessageScheduler`，绑定到逻辑线程 `io_context`                                                                                                                                                                                                                                                                                                                              |

# 批次 2026-10-23（基线 `38e522c`）：群资料获取与修改

> 完整字段、流程与已知问题见 [`api-group.md`](api-group.md) §9–§10。

## 1. 变更汇总

| 类别     | 内容                                                                                                                                                |
|----------|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| **新增** | TCP `1043`–`1046`：获取群资料、修改群资料                                                                                                           |
| **新增** | `struct GroupInfo`（`src/public/data/UserInfo.h`）、`Mysql_boost::getGroupInfo` / `changeGroupInfo`                                                 |
| **新增** | 存储过程 `update_group_info(group_id, operator_id, name, avatar_url, announcement, max_members, OUT error)`，已入库（`chatserver`）；DDL 不在本仓库 |
| **变更** | 存储过程返回码映射：`-1` → `1023`、`-2` → `1024`、`-3` → `1020`；其它负值见 §4                                                                      |
| **说明** | 群资料不缓存，`1043` 直查 `groups` 表；`1045` 不写系统消息、不推送 `1022`                                                                           |

## 2. TCP 消息

| message_id | 名称                            | 方向            | 本批变化       |
|------------|---------------------------------|-----------------|----------------|
| `1043`     | `MESSAGE_GET_GROUP_INFO_REQ`    | 客户端 → 服务端 | **新增枚举值** |
| `1044`     | `MESSAGE_GET_GROUP_INFO_RSP`    | 服务端 → 客户端 | **新增枚举值** |
| `1045`     | `MESSAGE_GROUP_INFO_CHANGE_REQ` | 客户端 → 服务端 | **新增枚举值** |
| `1046`     | `MESSAGE_GROUP_INFO_CHANGE_RSP` | 服务端 → 客户端 | **新增枚举值** |

## 3. 请求字段速查

| 接口              | 请求字段                                   | 成功回包字段                                                                |
|-------------------|--------------------------------------------|-----------------------------------------------------------------------------|
| 获取群资料 `1043` | `gid`                                      | `name`、`avatar`、`owner_uid`、`announcement`、`maxMb`、`mbCnt`、`createAt` |
| 修改群资料 `1045` | `gid`、`name`?、`avatar`?、`announcement`? | 仅 `error`                                                                  |

- `?` 表示可选： **字段缺省或为 `null` 时保持原值**（C++ 侧空 `std::optional` → SQL `NULL` → 存储过程 `COALESCE`）。
- 传空串 `""` 会真正覆盖为空串（可清空头像 / 公告）；群名传空串返回 `1020`。
- 修改群资料用 `avatar`，创建群聊用 `avator`（拼写错误）；获取群资料 / 群成员信息用 `gid`。

## 4. 错误码与已知问题

| error  | 获取群资料 `1044` | 修改群资料 `1046`            |
|--------|-------------------|------------------------------|
| `0`    | 查询成功          | 修改成功                     |
| `1011` | 未登录            | 未登录                       |
| `1020` | `gid` 解析失败    | `gid` 解析失败 / 群名为空串  |
| `1023` | 群不存在          | 群不存在                     |
| `1024` | ——                | 不是群主 / 管理员            |
| `1013` | 数据库不可用      | **不返回**（回包缺 `error`） |

- 修改群资料在存储过程返回 `-10` 或连接 / 执行异常（`-11`）时 **不写 `error` 字段**，回包只有 `msgid`；客户端需把"缺
  `error`"当作失败，见 [`api-group.md`](api-group.md) §13.12。
- 获取群资料不校验成员身份，任意已登录用户按群 id 即可查询（§13.13）。
- `groups` 表主键是 `id`（`group_members.group_id` 指向它），时间列是 `created_at`；`getGroupInfo` 已按该结构取数。
- `max_members` 在接口层固定传 `NULL`，本批不支持扩容（§13.14）。

# 批次 2026-10-23（基线 `15b4d72`）：群聊

> 本批包含两次提交：`6ca1a5c`（群聊创建 / 拉人 / 踢人 / 解散 / 群消息收发）与 `15b4d72`（获取群成员信息）。
> 完整字段、流程与已知问题见 [`api-group.md`](api-group.md)。
>
> 本批之后已修复：拉人入群的成员 uid 解析目标改为 `member_uid`；解散成功后 `DEL chatgroup_<gid>` 清理 Redis 集合；
> 群消息在 Stream 与 `1024` 回包中携带 `group_id`；通知逻辑抽到 `notifyMessageSend`，群聊遍历全部成员且不再提前退出；
> 创建群聊改用 `CALL create_group(...)`；群操作写入系统消息（`from_uid = 1`），踢人 / 解散改为先发系统消息并推送 `1022`
> ，再更新成员集合。

## 1. 变更汇总

| 类别       | 内容                                                                                                                                                  |
|------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| **新增**   | TCP `1033`–`1042`：创建群聊、拉人入群、踢人、解散、获取群成员信息                                                                                     |
| **新增**   | 错误码 `1023 GROUP_ID_INVALID`、`1024 NO_POWER`、`1025 GROUP_FULLED`（预留未使用）                                                                    |
| **新增**   | Redis 群成员集合 `chatgroup_<gid>`（Set，无 TTL；解散成功时 `DEL`）                                                                                   |
| **变更**   | `1013` 新增布尔字段 `group`：`true` 时 `to_uid` 传群 id，用 `SISMEMBER chatgroup_<gid>` 校验成员（不再校验好友）                                      |
| **变更**   | `1024` 的 `msgs[]` 新增布尔字段 `group` 与群 id 字段 `group_id`；群消息与单聊共用同一条消费流 `offlinestream_<uid>`                                   |
| **变更**   | 群消息投递方式：按 `SMEMBERS chatgroup_<gid>` 给 **每个成员（含发送者）** 各写一条 `offlinestream_<uid>`，Stream 字段新增 `group=y`、`group_id=<gid>` |
| **新增**   | 群操作系统消息：创建 / 拉人 / 踢人 / 解散成功后以 `from_uid = 1` 写入群消息流（先发消息并推送 `1022`，再更新成员集合；被踢者 / 解散成员也能收到）     |
| **变更**   | 通知逻辑抽到 `notifyMessageSend`：群聊遍历全部成员，离线 / 本实例无会话均 `continue`，不再提前退出；系统消息也会触发 `1022`                           |
| **变更**   | 创建群聊的存储过程由 `update_user_info` 改为 `create_group`                                                                                           |
| **废弃**   | 独立群离线流 `offlinestream_group_<gid>`（宏被注释，代码不再使用）                                                                                    |
| **未实现** | 主动退群、群列表、群资料修改、设置管理员、入群验证 / 邀请通知、群消息历史                                                                             |

## 2. TCP 消息

| message_id | 名称                              | 方向            | 本批变化                           |
|------------|-----------------------------------|-----------------|------------------------------------|
| `1013`     | `MESSAGE_TEXT_CHAT_MSG_REQ`       | 客户端 → 服务端 | 新增 `group` 字段与群成员校验      |
| `1014`     | `MESSAGE_TEXT_CHAT_MSG_RSP`       | 服务端 → 客户端 | 群消息发送回执（格式不变）         |
| `1024`     | `MESSAGE_SYNC_CHAT_MESSAGE_RSP`   | 服务端 → 客户端 | `msgs[]` 新增 `group` / `group_id` |
| `1033`     | `MESSAGE_GROUP_CREATE_REQ`        | 客户端 → 服务端 | **新增枚举值**                     |
| `1034`     | `MESSAGE_GROUP_CREATE_RSP`        | 服务端 → 客户端 | **新增枚举值**                     |
| `1035`     | `MESSAGE_GROUP_ADD_MEMBERS_REQ`   | 客户端 → 服务端 | **新增枚举值**                     |
| `1036`     | `MESSAGE_GROUP_ADD_MEMBERS_RSP`   | 服务端 → 客户端 | **新增枚举值**                     |
| `1037`     | `MESSAGE_GROUP_KICK_MEMBER_REQ`   | 客户端 → 服务端 | **新增枚举值**                     |
| `1038`     | `MESSAGE_GROUP_KICK_MEMBER_RSP`   | 服务端 → 客户端 | **新增枚举值**                     |
| `1039`     | `MESSAGE_GROUP_DISMISS_REQ`       | 客户端 → 服务端 | **新增枚举值**                     |
| `1040`     | `MESSAGE_GROUP_DISMISS_RSP`       | 服务端 → 客户端 | **新增枚举值**                     |
| `1041`     | `MESSAGE_GET_GROUP_USER_INFO_REQ` | 客户端 → 服务端 | **新增枚举值**                     |
| `1042`     | `MESSAGE_GET_GROUP_USER_INFO_RSP` | 服务端 → 客户端 | **新增枚举值**                     |

## 3. 请求字段速查

| 接口                  | 请求字段                                     | 成功回包字段         |
|-----------------------|----------------------------------------------|----------------------|
| 创建群聊 `1033`       | `name`、`avator`（拼写如此）、`announcement` | `group_id`（string） |
| 拉人入群 `1035`       | `group_id`、`members`（array\<string\>）     | 仅 `error`           |
| 踢人 `1037`           | `group_id`、`uid`                            | 仅 `error`           |
| 解散 `1039`           | `group_id`                                   | 仅 `error`           |
| 获取群成员信息 `1041` | `gid`（**不是 `group_id`**）                 | `mb_infos`（array）  |

- 群消息发送（`1013`）新增 `group`（bool，缺省 `false`）；`to_uid` 在群聊时传群 id。
- 所有响应都带 `msgid` 原样回显；未登录返回 `1011`。

## 4. 主要错误码映射

| 接口       | 存储过程返回                        | 对外 `error`                                   |
|------------|-------------------------------------|------------------------------------------------|
| 创建群聊   | `-1` / 其它                         | `1011` / `1013`                                |
| 拉人入群   | `-1` / `-2` / `-3` / `-4` / 其它    | `1023` / `1024` / 跳过继续 / `1013` / `1013`   |
| 踢人       | `-1` / `-2` `-4` `-5` / `-3` / 其它 | `1023` / `1024` / `1014` / `1013`              |
| 解散       | `-1` / `-2` / 其它                  | `1013`（被覆盖，预期 `1023`）/ `1024` / `1013` |
| 群成员信息 | Redis 不在群 / MySQL 失败           | `1024` / `1013`                                |

## 5. 群消息存储变化

| 项       | 本批之前                              | 本批之后                                                                 |
|----------|---------------------------------------|--------------------------------------------------------------------------|
| 群消息流 | `offlinestream_group_<gid>`（未启用） | 每个成员各自的 `offlinestream_<uid>`（`group=y`），一次 pipeline 批量写  |
| 消息字段 | `from_uid` / `type` / `body`          | 新增 `group`（`f` 单聊 / `y` 群聊）                                      |
| 群 id    | ——                                    | 以 `group_id` 字段写入每条群消息，`1024` 的 `msgs[].group_id` 带回       |
| 发送者   | ——                                    | 发送者本人也在成员集合内，会收到自己发出的群消息                         |
| 系统消息 | ——                                    | 创建 / 拉人 / 踢人 / 解散写入 `from_uid=1` 的文本消息，并全部推送 `1022` |

## 6. 内部变更（不影响对外契约）

| 内容                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `src/public/const.h`：新增 `ErrorCode::GROUP_ID_INVALID` / `NO_POWER` / `GROUP_FULLED`，新增 `MESSAGE_GROUP_*`（`1033`–`1040`）与 `MESSAGE_GET_GROUP_USER_INFO_*`（`1041` / `1042`）                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `src/chatServer/LogicSystem.cpp`：新增 `createChatGroupHandler` / `chatGroupAddMembersHandler` / `chatGroupKickMemberHandler` / `chatGroupDismiss`（后改名 `chatGroupDismissHandler`）/ `chatGroupGetUserInfosHandler` 并在 `registerCallBacks` 注册；`sendTextMessageHandler` 增加 `group` 分支、群成员校验与按成员扇出；通知逻辑抽到 `notifyMessageSend(is_group, to_id)`，群聊遍历全部成员、不再提前退出；`syncChatMessageHandler` 回包增加 `group` / `group_id`；群操作写入 `from_uid=1` 的系统消息并调用通知（踢人 / 解散为先发消息并通知、后改成员集合）；新增未使用的 `sendSystemMessage` 辅助函数 |
| `src/public/src/RedisManager_boost.*`：新增 `insertToGroup` / `getUsersFromGroup` / `kickUserFromGroup` / `removeGroup` / `isInGroup` 与 `setMembers`、`streamsAdd`（pipeline 批量 `XADD`）；群消息写入 `group_id`，`getMessageFromOffline` 解析回 `Message::to_uid`；`GROUP_OFFLINE_MESSAGE_STREAM` 宏注释废弃，新增 `CHAT_GROUP_PERFIX`（`chatgroup_`）                                                                                                                                                                                                                                                 |
| `src/public/MessageType.h`：`chat_message::Message::to_uid` 语义明确为「单聊接收方 uid / 群聊群 id」，并补默认初始化                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `src/public/sql/mysql/Mysql_boost.*`：新增 `createChatGroup`（`CALL create_group(...)`）/ `addGroupMember` / `kickGroupMember` / `dismissGroup` / `getGroupMemberInfos`                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `src/public/data/UserInfo.h`：新增 `struct GroupUserInfo`（`nick_g` / `joinedAt` / `uid` / `role`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |

## 7. 已知限制（摘要）

1. **解散时群不存在返回 `1013`**：错误码被后续 `else` 覆盖（§7.3）。
2. **解散清理 key 用请求原串**：带前导零的 `group_id` 会漏删真实 key；`DEL` 失败时回包缺 `error`（§7.3）。
3. **群成员集合无 TTL、无重建**：Redis 重启 / 清空后群聊全部失效（§12）。
4. **无主动退群、群列表、群资料修改、管理员设置、入群通知**；群人数上限硬编码 500。

# 批次 2026-10-21（基线 `5524366`）：好友备注与用户资料编辑

## 1. 变更汇总

| 类别       | 内容                                                                                                           |
|------------|----------------------------------------------------------------------------------------------------------------|
| **新增**   | TCP `1029` / `1030` 设置好友备注；TCP `1031` / `1032` 编辑用户资料                                             |
| **变更**   | 所有 TCP 响应统一回显客户端请求编号 `msgid`（含心跳）；业务处理器不再单独回显 `id`                             |
| **变更**   | 用户资料缓存读取抽到 `RedisManager::getUserInfo`（`SearchUserByUid` 改为调用它，Redis 解析异常改为回落 MySQL） |
| **变更**   | 每次通过 `getUserInfo` 读取用户资料时，把 `ubaseinfo_<uid>` 的 TTL 续期为 **24 小时**（读即续期）              |
| **变更**   | 编辑资料成功后由 `RedisManager::setUserBaseInfo` 回写 `ubaseinfo_<uid>`，TTL **24 小时**，并保留 `token` 字段  |
| **变更**   | 用户名索引 `ubaseinfo_name_<name>` 的默认 TTL 由 `1800s` 调整为 **7 天**（`7 * 24 * 60 * 60`）                 |
| **未实现** | 备注 / 资料变更的推送通知；改名后旧名字索引的清理；头像等字段的内容校验                                        |

字段与流程详见本文 §3–§5，好友备注另见 [`api-friend.md`](api-friend.md) §10。

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
  "msgid": "c-0001",
  "friend_uid": "10002",
  "remark": "老同学"
}
```

| 字段         | 类型   | 必填 | 说明                                                               |
|--------------|--------|------|--------------------------------------------------------------------|
| `msgid`      | 任意   | 否   | 客户端定义的请求唯一编号，服务端**原样回显**（不做解析，类型不变） |
| `friend_uid` | string | 是   | 好友 uid，必须是纯数字字符串，否则返回 `1020`                      |
| `remark`     | string | 否   | 新备注；**缺失或为 `null` 时按空串处理，即清空备注**               |

- 设置方取当前会话已登录 uid（`session->getUserId()`），请求体不需要传自己的 uid。
- 只修改 **当前用户视角**的单向备注（`user_friend.remark`），不会影响对方给本人设置的备注。

### 3.2 响应

```json
{ "error": 0, "msgid": "c-0001" }
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
  "msgid": "c-0002",
  "name": "新昵称",
  "desc": "新签名",
  "icon": "http://example.com/icon.png",
  "sex": 1
}
```

| 字段    | 类型   | 必填 | 说明                                                                                        |
|---------|--------|------|---------------------------------------------------------------------------------------------|
| `msgid` | 任意   | 否   | 客户端定义的请求唯一编号，服务端**原样回显**                                                |
| `name`  | string | 否   | 昵称                                                                                        |
| `desc`  | string | 否   | 个性签名                                                                                    |
| `icon`  | string | 否   | 头像地址                                                                                    |
| `sex`   | int    | 否   | 性别，见 [`UserInfo.h`](../src/public/data/UserInfo.h) 的 `Sex`：`0` 男 / `1` 女 / `2` 未知 |

- **所有字段均可选**：字段缺失或显式传 `null` 表示不修改该项（`Json::Value::isNull()` 判定）。
- 全部字段缺失时仍会调用 Redis 与 MySQL（等价于一次空更新），返回值取决于存储过程，通常为 `0`。
- 修改对象固定为当前会话已登录 uid，请求体不传 uid。
- 字段长度、内容与 `sex` 取值范围均 **未校验**，由存储过程与数据库约束决定。

### 4.2 响应

```json
{ "error": 0, "msgid": "c-0002" }
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

## 5. 统一响应编号 msgid

所有 TCP 请求都可以在 body 中携带 `msgid`，由 **客户端生成并保证唯一**（递增序号、UUID 等均可，建议用字符串）；服务端在所有响应中原样回显。

```json
{ "msgid": "c-0003", "uid": "10002" }
```

```json
{ "error": 0, "msgid": "c-0003" }
```

| 项   | 行为                                                            |
|------|-----------------------------------------------------------------|
| 回显 | 所有接口（含心跳），成功 / 失败 / 未登录响应都带 `msgid`        |
| 类型 | 原样拷贝，不做解析：传字符串回字符串，传数字回数字              |
| 缺失 | 请求未带 `msgid` 时，响应中为 `null`                            |
| 时机 | 在业务处理前写入，因此 `1011`（未登录）等错误响应同样带 `msgid` |

- 实现位置：`LogicSystem.h` 的 `HANDLER_PREFIX` / `HANDLER_PREFIX_WITHOUT_CHECK_LOGIN` 宏，统一执行
  `return_value["msgid"] = root["msgid"]`；`heartbeatHandler` 也已改用 `HANDLER_PREFIX`。
- 客户端应按 `msgid` 匹配请求与响应，不要依赖到达顺序；同一连接内服务端串行处理，回包顺序与请求一致。
- 服务端主动推送（`1009` / `1012` / `1022`）不是请求的响应， **不带 `msgid`**。
- 与 `message_id`、`msg_id` 的区别：
    - `message_id`：帧头里的消息类型编号（`1001`–`1042`）；
    - `msgid`：请求级唯一编号，所有接口通用；
    - `msg_id`：聊天消息业务编号，仅 `1013` / `1014` 使用（发送回执仍会单独回显，见 [`api-chat.md`](api-chat.md)）。
- 本批之前按业务字段回显的编号（删除好友回包的 `uid`、发送聊天回包的 `msg_id`）保持不变，可能与 `msgid` 同时出现。

## 6. 已知限制（本批新增）

1. **编辑资料依赖前置查询**：`RedisManager::changeUserInfo` 读不到 `ubaseinfo_<uid>` 时返回 `false`（`1013`
   ）且不写库。按约定客户端编辑前会先查询用户资料（查询会把缓存写入 / 续期），实际不会命中该分支；若跳过查询且缓存已过期，则仍会失败。
2. **缓存 TTL 统一为 24h**：登录 / 搜索回源数据库与编辑资料回写都走 `setUserBaseInfo`（24h），`getUserInfo` 每次读取再续期
   24h；只要用户 24h 内有过一次资料查询或登录，缓存就不会过期。
3. **Redis 故障时回包缺少 `error` 字段**：`getUserInfo` 的 `expire` 与 `setUserBaseInfo` 的 `SET` / `EXPIRE`
   都在异常捕获之外，Redis 不可用时会抛出，由 `dealMessage` 统一捕获；此时客户端收到只含 `msgid`、 **没有 `error`
   字段**的回包（不会返回 `1013`），客户端需按缺省失败处理。登录路径由 `GetBaseInfo` 的 try 兜底（回落数据库），不受影响。
4. **改名不清理旧索引**：`ubaseinfo_name_<旧名>` 会保留至 TTL（7 天）到期，期间按旧用户名仍能搜到该用户。
5. **弱一致**：Redis 先写、MySQL 后写，MySQL 失败时缓存已变更，不回滚。
6. **无字段校验**：`name` / `desc` / `icon` 未做长度与内容过滤，`sex` 未校验是否属于 `0` / `1` / `2`；`remark`
   缺失即清空备注，请客户端显式传值。
7. **缓存内容解析失败不会回落 MySQL**：`getUserInfo` 只捕获异常，`Json::Reader::parse` 返回 `false` 时仍会用空
   `Json::Value` 构造结果（`uid = 0`、字段为空串）。此时 `SearchUserByUid` 返回的是这份空资料，而不是数据库数据。

## 7. 内部变更（不影响对外契约）

| 内容                                                                                                                                                                                                                                                                                                                                                                            |
|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `src/public/const.h`：新增 `MESSAGE_SET_FRIEND_REMARK_REQ` / `_RSP`（`1029` / `1030`）与 `MESSAGE_CHANGE_USER_INFO_REQ` / `_RSP`（`1031` / `1032`）                                                                                                                                                                                                                             |
| `src/chatServer/LogicSystem.h` / `.cpp`：`HANDLER_PREFIX` / `HANDLER_PREFIX_WITHOUT_CHECK_LOGIN` 统一回显 `msgid`，各处理器不再单独回显 `id`；删除未使用的 `HANDLER_PREFIX_WITHOUT_RSP`；`heartbeatHandler` 改用 `HANDLER_PREFIX`；新增 `setFriendRemarkhandler`、`changeUserInfoHandler` 并在 `registerCallBacks` 注册；`SearchUserByUid` 改为调用 `RedisManager::getUserInfo` |
| `src/public/src/RedisManager_boost.*`：新增 `getUserInfo(uid)`（原 `SearchUserByUid` 中的缓存解析逻辑下沉，读取前把 key 续期 24h，保留 `token`，异常返回 `nullopt`）、`setUserBaseInfo(uid, info)`（写 `ubaseinfo_<uid>` 并设 TTL 24h）与 `changeUserInfo(...)`；`insert` 默认 TTL 改为 7 天；大量 `[[nodiscard]]` 标注                                                         |
| `LogicSystem::GetBaseInfo` 改为只接收 `uid`：缓存读取走 `RedisManager::getUserInfo`，回源数据库后走 `setUserBaseInfo`；`setFriendRemarkhandler` 响应改用 `MESSAGE_SET_FRIEND_REMARK_RSP`（`1030`）                                                                                                                                                                              |
| `src/public/sql/mysql/Mysql_boost.*`：新增协程版 `changeUserInfo`（`CALL update_user_info(?, ?, ?, ?, ?, @error)`，`std::optional` 参数直接绑定为 `NULL`）                                                                                                                                                                                                                      |
| `RedisManager_boost.h` 引入 `data/UserInfo.h`（对外暴露 `std::optional<UserInfo>`)                                                                                                                                                                                                                                                                                              |

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
