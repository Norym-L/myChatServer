# 待拍板 / 本轮刻意没改的清单

按「影响面 × 改动成本」排序。每条都附了现状证据，可以直接照着定。

## 状态一览（2026-10-25 迁移 000–012 已应用到真库之后）

| #  | 事项                                           | 状态                                                                                                               |
|----|------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| 1  | `user.name`/`email` 唯一性                     | **已决（2026-10-25）：只做 `uid` 唯一**，name/email 都不加唯一约束，`pending/100_*.sql` 已删除                     |
| 2  | `message` 四表接上还是删掉                     | **已接上 `message`**（012 + `sendTextMessageHandler` 落库）；另外三张（附件/文件/上传会话）仍 0 行无写入，待定     |
| 3  | 文件两套状态机                                 | **待拍板**（本轮只统一类型/注释）                                                                                  |
| 4  | 一条好友关系散在 4 行 2 表                     | **待拍板**，建议单独排期（牵动 12 个过程）                                                                         |
| 5  | 无会话表 / 已读模型                            | **待拍板**；消息落库后新增一项：历史读取接口还没做（见 §5）                                                        |
| 6  | `update_group_info` 形参宽度 255 vs 列 150/512 | **待修**，改了要与 002 一样重建过程                                                                                |
| 7  | uid 1001 的 `sex=1` 是男是女                   | **已决（2026-10-25）：按服务器代码走** = `FEMALE`（女），数据不动                                                  |
| 8  | `message.status` / `msg_type` 语义             | **已决（2026-10-25）**：`msg_type` 对齐 `chat_message::Type`，`status` = 已入库存档，投递/已读不归它管（迁移 012） |
| 9  | `member_count` 是否自动对账                    | **待定**，当前值已修正，`checks/` 里有一键对账                                                                     |
| 10 | 全库 DATETIME 的时区约定                       | 已统一为 `datetime`，海外用户场景需另做一次整体迁移                                                                |

已处理完、不再需要拍板的（记录在此避免重复讨论）：collation 混用、`desc` 保留字 → `bio`、
`sex` 域与代码对齐、`user` 缺时间戳、`user_friend` 无主键、`member_count` 漂移（已修数据）、
`groups`/`group_members`/`friend_request`/`message` 缺外键、`char(36)` 的 144 字节键、bigint 符号不一致、
`message` 多态会话索引、消息落库与客户端重试幂等。

---

## 1. `user.name` / `user.email` 要不要唯一？——已决：都不唯一，只有 uid 唯一

**2026-10-25 决定（用户拍板）**：「邮箱只用做接收验证码，找回用账号」「只有 uid 唯一，别的都不唯一」。

所以：

- **不建** `UNIQUE(name)`、 **不建** `UNIQUE(email)`。身份的唯一键只有 `uid`。
- 一个邮箱开多个账号是刻意的：`reg_user` 里的 `IF v_exist > 5 THEN -1` 是防滥用上限，
  不是笔误，不要改。
- 「找回密码」用账号（uid）而不是邮箱。
- 按名字查用户 **允许多行**：`getUser(std::string name)` 本来就返回 `vector<UserInfo>`，
  语义正好对上；调用方不要假设单行。

**遗留的小坑（接受，不再处理）**：`norrmm` 有两个账号（1002、666675）、`357***@qq.com` 挂在三个
uid 上（1001、1002、666675）。这不是脏数据，是允许的表达。若以后有人按 name 拼登录，
会命中多行 —— 那是登录接口要处理的事，不是数据库约束的事。

> 原来的 `migrations/pending/100_identity_uniqueness.sql` 已删除：方向被否掉了，留着容易误执行。

---

## 2. `message` / `message_attachment` / `file_object` / `upload_session` ——已接上 `message`

**现状（2026-10-25 之后）**：`message` 表已由 `LogicSystem::sendTextMessageHandler` 写入，
每次成功投递一条历史；客户端重试靠 `(sender_id, client_msg_id)` 唯一键幂等。
细节见 `README.md` 的「消息落库」一节。

**仍然待定**：`message_attachment` / `file_object` / `upload_session` 三张表还是 0 行、仍然没有代码写入。
它们对应「图片/文件消息」的存储，等真要发图时再定：

- 要接上：需要文件上传链路（`ResourceServer`）+ 两套状态机合并成一套（见 §3）。
- 不接上：建议 DROP，别让人误以为附件有持久化。

`message.content` 现在对图片/文件类型存的是引用信息（`object_key` 或 JSON），
等附件表接上后再决定要不要把引用拆成外键。

## 3. 文件这块有两套状态机

`file_object.status`（0待上传/1可用/2已删除）+ `upload_session.status`（0待确认/1已确认/2已过期）
同一个「待办」状态存两份，必然漂移。另外 `upload_session` 没有 `owner_id`（谁传的查不出来，
没法做配额/限速），也没有 `UNIQUE(file_id)`（同一文件能开 N 个会话）。
本轮只统一了类型和注释，没动结构 —— 因为没代码写入，怎么设计都不算错，等你定。

## 4. 一条好友关系散在 4 行、2 张表里

`friend_request` 有 `UNIQUE(applicant_uid, receiver_uid)`，所以「A 申请 B」永远只有一行：
重发时存储过程走 `UPDATE` 而不插新行， **且不更新 `created_at`**，于是「申请时间」永远是最早那次；
`handle_friend_request` 同意时还会把 **反向** pending 一起置 1，`user_friend` 再双向插两行。
一条关系 = 2 行 `friend_request` + 2 行 `user_friend`，没有单一真相。

可选：改成单行关系模型（`(uid, peer_uid)` 唯一 + 方向列），或在 `user_friend` 上用 `(uid, friend_uid)` 单向
存一条、查询时双向 OR。 **这是模型级改动，会牵动 12 个存储过程，建议单独排期。**

## 5. 没有会话表和已读模型（消息落库后新增一项：历史读取接口）

会话列表要的「最后一条消息、未读数、置顶/静音、排序」现在无处安放；`message.status` 这一列
也表达不了群里「每个成员各自的已读」（1 列存不下 N 个成员的已读位）。要做已读回执，
必须引入 `session` + `session_member`（每成员一行的 last_read_message_id / unread_count）。

**新增待办（消息落库带出来的）**：现在有了 `conversation_id` 生成列和
`idx_conversation(conversation_id, id)`，写侧已经能按会话翻页了，但 **读侧还没接**——
`LogicSystem::syncChatMessageHandler`（`MESSAGE_SYNC_CHAT_MESSAGE_REQ` / `1023`）
仍然只从 Redis（`offlinestream_*` + PEL）拉消息，历史记录查 MySQL 的接口还没实现。
要做的时候直接用：

```sql
SELECT id, sender_id, msg_type, content, created_at
FROM message
WHERE conversation_id = ?          -- 群聊 'g3' / 单聊 'u1001:666684'
  AND id < ?                       -- 翻页游标（上一页最小的 id）
ORDER BY id DESC LIMIT ?
```

`EXPLAIN` 已确认走 `idx_conversation` + `Backward index scan`，无 filesort。

## 6. `update_group_info` 形参宽度与列不一致

存储过程形参 `p_name varchar(255)` / `p_avatar_url varchar(255)`，但列是 `varchar(150)` / `varchar(512)`。
超长时会在 UPDATE 处撞 `ERROR 1406 Data too long`，被 EXIT HANDLER 吞成 `-10 数据库异常`，
错误码看不出真实原因。建议把形参改成与列一致（`150` / `512`），顺手在过程里做长度校验。

## 7. `uid 1001` 的 `sex=1` ——已决：是「女」，数据不动

**2026-10-25 决定**：「按服务器来」。服务器代码 `enum Sex { MALE, FEMALE, UNKNOWN }` = 0/1/2
（`src/public/data/UserInfo.h`），客户端 `UserInfoDialog.qml` 的 `sexValue()` 也是 0→男、1→女，
docs 同样是 0/1/2。所以 `sex=1` = **女**，迁移 003 之后的当前值是对的，不需要 UPDATE。

（原 DDL 注释写的「1男/2女/3未知」是错的，已被迁移 012 之前的 003 改写为代码语义。）

## 8. `message.status` / `message.msg_type` ——已决：见迁移 012

- `msg_type`：对齐 `chat_message::Type` —— `1文本 / 2图片 / 3文件 / 4表情包`，`0=未知`（历史数据）。
- `status`： **只表示「这行已入库存档」**，恒为 0。在线投递、离线拉取（PEL）、ACK、已读
  全部由 Redis 负责；Redis 才是投递真相，MySQL 只负责历史。等真要做已读回执时再按 §5
  引入 `session_member`，不要在这个 tinyint 上叠语义。

## 9. `member_count` 要不要自动对账

迁移 006 修好了当前漂移，但 **只要还有「绕过存储过程直接改表」的路径，它就会再漂**。
两个选项：① 定时跑 `db/checks/check_member_count_drift.sql` 报警；
② 干脆去掉这个冗余列，改成 `COUNT(*)`（现在群规模 ≤ 500，成本可接受）。

## 10. 全库时间类型统一成 DATETIME 了，时区语义要约定

8 张表本来就是 `datetime`（无时区语义，存的是服务器本地时间 CST）。迁移 009 把
`friend_request` 的两个 `timestamp` 也改成了 `datetime`，做到「全库一致」。
代价是： **应用层必须自己保证时区一致**（现在 `@@time_zone=SYSTEM` = CST）。
以后要支持海外用户，正确做法是全库改 `timestamp`（UTC）+ 连接层固定 `time_zone='+00:00'`，
那是另一次整体迁移。
