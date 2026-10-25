# db/ —— chatserver 数据库结构版本化

## 为什么有这个目录

2026-10-25 之前，`chatserver` 的 **表结构和 13 个存储过程只存在于数据库里**，仓库里没有任何
`CREATE TABLE` / `CREATE PROCEDURE`（`docs/api-changes.md:72` 当时就写了「DDL 不在本仓库」）。
后果是换台机器、删了容器就重建不出来，改存储过程也没法 review / 回滚 / 先在测试库验证。

现在起： **结构改动必须落成这个目录里的迁移文件**，数据库只是它的执行结果。

## 目录

```
db/
├── README.md                 本文件
├── decisions.md              需要产品/架构拍板、暂时没法自动改的清单
├── apply.sh                  应用未执行的迁移
├── schema.sql                迁移后的完整结构（生成物，不要手改）
├── baseline/
│   └── 000_before_20261025.sql   改动前的完整结构与存储过程快照
├── migrations/
│   ├── 000_schema_migrations.sql
│   ├── 001_unify_collation.sql
│   └── ...                   按序号执行，一个文件只做一件事
└── checks/                   体检 SQL（对账 / 查重 / 查孤儿），正常都应返回 0 行
```

## 日常操作

连接信息（本机开发环境）：容器 `gateserver-mysql`、库 `chatserver`、`root/123456`、宿主端口 3308。

```bash
# 1) 应用未执行的迁移（幂等：已记录的会跳过）
./db/apply.sh

# 2) 先演练：把当前库克隆一份，在克隆上跑一遍再碰真库
docker exec -i gateserver-mysql mysqldump -uroot -p123456 --routines --triggers \
  --single-transaction --default-character-set=utf8mb4 chatserver > /tmp/now.sql
docker exec -i gateserver-mysql mysql -uroot -p123456 -e \
  "DROP DATABASE IF EXISTS chatserver_dryrun; CREATE DATABASE chatserver_dryrun CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
docker exec -i gateserver-mysql mysql -uroot -p123456 chatserver_dryrun < /tmp/now.sql
MYSQL_DB=chatserver_dryrun ./db/apply.sh

# 3) 改动后刷新 schema.sql（它是生成物）
docker exec gateserver-mysql mysqldump -uroot -p123456 --routines --triggers --events \
  --no-data --skip-comments --single-transaction --default-character-set=utf8mb4 chatserver \
  > db/schema.sql

# 4) 改结构之前先备份（含数据 + 存储过程）
docker exec gateserver-mysql mysqldump -uroot -p123456 --routines --triggers --events \
  --single-transaction --default-character-set=utf8mb4 chatserver \
  > db/backups/chatserver-$(date +%Y%m%d-%H%M%S).sql
```

## 约定

1. **迁移只增不改**。已经应用过的文件不要回头编辑（数据库里可能已经跑过别的版本）。
2. 文件名 `NNN_简短英文描述.sql`，序号连续；一个文件只做一件事，便于回滚定位。
3. 每个迁移文件头部写清：为什么改、影响哪些代码、怎么回滚。
4. 文件末尾往 `schema_migrations` 写一行版本号（照抄现有文件的写法）。
5. **结构 + 数据修复前先跑 `checks/` 里的体检**，确认约束能加上（外键最怕孤儿行）。
6. 改了表结构，同一个 PR 里必须同步改：`db/schema.sql`（重新导出）、以及任何直接拼 SQL 的 C++ 代码。
7. `db/backups/` 不进版本库（已加 .gitignore），里面是含用户数据的明文。

## 这份迁移覆盖了什么

| 迁移 | 内容                                                                                                                |
|------|---------------------------------------------------------------------------------------------------------------------|
| 001  | 全库 collation 统一到 `utf8mb4_0900_ai_ci`（原来两套混用，跨表比较直接 ERROR 1267）                                 |
| 002  | `user.desc` → `user.bio`（DESC 是保留字）、`icon` 4096 → 512、重建 `update_user_info`                               |
| 003  | `user.sex` 域对齐代码 `0男/1女/2未知`（原默认 3、注释与代码/客户端/docs 全不一致），加 CHECK                        |
| 004  | `user` 补 `created_at` / `updated_at`                                                                               |
| 005  | `user_friend` 显式主键 + `created_at`                                                                               |
| 006  | 数据修复：`groups.member_count` 按 `group_members` 重算（已漂移过）                                                 |
| 007  | `groups` 补 owner 外键、`member_count <= max_members` CHECK、群名索引                                               |
| 008  | `group_members` 补外键、`nickname` 50 → 64                                                                          |
| 009  | `friend_request` timestamp → datetime（全库统一）、补双向外键                                                       |
| 010  | 文件四表：UUID 改 ascii（144 → 36 字节）、有符号 → 无符号、状态注释                                                 |
| 011  | `message` 多态会话索引 + 发送者外键（该表当时 0 行、无代码写入）                                                    |
| 012  | **消息落库**：`client_msg_id` 幂等键 + `conversation_id` 生成列 + 会话分页索引，删两个冗余索引；配套 C++ 改造见下节 |

还没做的（要拍板）：见 [decisions.md](decisions.md)。

## 2026-10-25 执行记录（真库 `chatserver` 已应用）

迁移前备份：`db/backups/chatserver-before-apply-20261025-213049.sql`（50,601 字节，9 表 13 过程）。
另有一份更早的 `db/backups/chatserver-before-20261025-212614.sql`。

先演练后上线，两个库都验证过：

| 检查项                       | 演练库 `chatserver_dryrun`                      | 真库 `chatserver`         |
|------------------------------|-------------------------------------------------|---------------------------|
| 迁移应用                     | 000–011 共 12 个全部成功                        | 同左（21:30:50–21:30:56） |
| 表数                         | 10（9 + `schema_migrations`）                   | 10                        |
| 非 `utf8mb4_0900_ai_ci` 的表 | 0                                               | 0                         |
| 外键                         | 12                                              | 12                        |
| CHECK                        | 2（`ck_user_sex`、`ck_groups_member_count`）    | 2                         |
| 主键                         | 10 张表都有（含 `user_friend(uid,friend_uid)`） | 同左                      |
| `char(36)` 字符集            | 3 处均为 ascii                                  | 同左                      |

数据前后一致性（把迁移前备份还原到临时库逐表比对）：

- 9 张表行数 **全部不变**（user 13、user_friend 12、friend_request 11、groups 3、group_members 7，其余 0）。
- `user` 只有 sex 变了：11 个 `3` 归一为 `2`；uid 1001 的 `1`、uid 1002 的 `0` 原样保留。
- `groups` 只有 group 1「自检群」的 `member_count` 从 1 修正为 2（原本就是错的）。
- `friend_request` 11 条 `created_at`/`updated_at` **逐条完全一致**（timestamp → datetime 没有发生时区偏移）。
- 好友关系、群成员逐行一致。

存储过程冒烟（在演练库一个 mysql 会话里跑完整流程）：`reg_user err=0` → `update_user_info err=0`
（bio/icon/sex 写入、name 未变）→ `create_group` 1 人 → `add_group_member` 2 人 → 重复加人 `-3` →
`kick_group_member` 1 人 → `update_group_info err=0` → `send_friend_request` → `handle_friend_request`
（双向两行 user_friend）→ `update_friend_remark` → `del_friend` → `dismiss_group`（成员与群均清空）。

> 注意：`@` 用户变量 **不跨 `docker exec mysql -e` 会话**。分多次调用跑流程会得到一串假的 `err=-1`，
> 必须把整个流程写进一个 `.sql` 文件一次性喂给 mysql。

负向测试（6 条全部按预期被拒绝，坏数据一条没进去）：

| 尝试写入的坏数据                  | 结果                                                 |
|-----------------------------------|------------------------------------------------------|
| `UPDATE user SET sex=3`           | `ERROR 3819` Check constraint `ck_user_sex` violated |
| 给不存在的用户加群成员            | `ERROR 1452` `fk_gm_user`                            |
| 删除仍被群引用的用户              | `ERROR 1451` `fk_gm_user` ON DELETE RESTRICT         |
| `member_count` 超过 `max_members` | `ERROR 3819` `ck_groups_member_count` violated       |
| 不存在的申请人进 `friend_request` | `ERROR 1452` `fk_fr_applicant`                       |
| 不存在的 sender 进 `message`      | `ERROR 1452` `fk_msg_sender`                         |

同步改的 C++（`desc` 列已不存在，不改就 `ERROR 1054 Unknown column 'desc'`）：

- `src/public/sql/mysql/MySqlDao.cpp:311`、`MySqlDao.cpp:358`：`\`desc\`` → `\`bio\``
- `src/public/sql/mysql/Mysql_boost.cpp:173`、`Mysql_boost.cpp:216`：同上
- 四处 `row.at(4).is_null() ? 3 : ...` 的 sex 兜底值 `3` → `2`（3 已不是合法值）
- 取值是 **按列位置**（`row.at(2)`），不是按列名，所以 SELECT 换列名不影响读取；
  JSON 字段名仍是 `"desc"`（`LogicSystem.cpp` 里拼的）， **客户端零改动**。

演练库 `chatserver_dryrun` 用完已删除。

## 消息落库（迁移 012 + C++ 改造，2026-10-25）

**为什么要做**：迁移前 `message` 表 0 行、`src/` 里没有任何写入，聊天记录 **只在 Redis**。
Redis 是内存 + 离线队列（`offlinestream_*` / PEL），重启或裁剪就没了，等于没有历史。

**改了什么**：

| 位置                                        | 改动                                                                                                                                                                                  |
|---------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `db/migrations/012_message_persistence.sql` | 加 `client_msg_id`（幂等）、`conversation_id` 生成列（会话键）、`uk_sender_client` + `idx_conversation`；删冗余的 `idx_session` / `idx_sender`；补 `msg_type`/`status`/`content` 注释 |
| `src/public/sql/mysql/Mysql_boost.h/.cpp`   | 新增 `net::awaitable<int> saveMessage(sender_id, session_id, is_group, msg_type, content, client_msg_id)`：一条预处理 `INSERT ... ON DUPLICATE KEY UPDATE id=id`，1 次往返            |
| `src/public/sql/mysql/MySqlManager.h`       | 新增转发包装 `saveChatMessage(...)` → `co_await m_mysqlBoost.saveMessage(...)`                                                                                                        |
| `src/chatServer/LogicSystem.cpp`            | `sendTextMessageHandler` 里 Redis 投递成功后落一条历史；落库失败只 `SPDLOG_ERROR`，不影响回给客户端的 `SUCCESS`                                                                       |

**关键设计**：

- **Redis 是投递真相，MySQL 是历史真相**。落库放在投递之后、回包之前；落库失败不回滚投递，
  也不改错误码（消息已经发出去了，客户端不该因为存档失败而看到失败）。
- **`status` 恒为 0**，只表示「这行已入库」。在线投递、离线拉取、ACK、已读都不放在这一列（见 decisions #5/#8）。
- **`conversation_id` 用生成列而不是 C++ 拼**：`session_id` 是多态的（单聊=对方 uid、群聊=群 id），
  单聊两个方向必须归一，否则历史分页只能写 `(sender,session) OR (session,sender)`，索引用不上。
  群聊 `g<群id>`、单聊 `u<小uid>:<大uid>`；放成 STORED 后任何写入方都不会漏维护。
- **不写存储过程**：其余业务写都走 `CALL`，但 `callMysqlSp*` 的后置 `SELECT @error` 让成本固定 2 次往返。
  消息是最高频写入，一条预处理 INSERT 就够（`ON DUPLICATE KEY UPDATE id=id` 让重复消息影响行数为 0，
  代码里按 0 也算成功处理）。
- **客户端重试幂等**：`msg_id` 由客户端生成、服务端原样回显，落在 `(sender_id, client_msg_id)` 唯一键上。
  没带 `msg_id` 时写 NULL —— 唯一键允许多个 NULL，不会被误判重复。

**验证（真机跑通，不是只看 SQL）**：

- 结构：真库 `information_schema.COLUMNS` 确认 `conversation_id` 是 `STORED GENERATED`；
  索引只剩 `PRIMARY` / `uk_sender_client` / `idx_conversation`；`schema_migrations` 13 行（000–012）。
- SQL 层：重复 `(sender, client_msg_id)` 被吞；`client_msg_id=NULL` 可插多行；
  单聊双向都归一成 `u1001:1002`、群聊 `g3`；`EXPLAIN` 走 `idx_conversation` + `Backward index scan`（无 filesort）。
- 端到端：起 `StatusServer` + `ChatServer` + `GateServer`，注册临时账号发 4 条消息
  （单聊 2 条不同 msg_id、1 条重试、群聊 1 条）→ `message` 表落 **3 行**（重试那条被幂等吞掉），
  `conversation_id` 分别是 `u1001:666684` 和 `g3`。测试数据、临时账号、Redis 键全部清理，群人数恢复原值。
- 复现脚本：`tools/e2e-message-test.mjs`（Node，跑在 Windows 侧；注册 → 登录 → 单聊+重试 → 群聊 →
  打印 `msg_id` 与回包）。用法见脚本头部注释。

**还没接的**（都记在 decisions 里）：

- 历史 **读**接口：`LogicSystem::syncChatMessageHandler`（1023）仍只读 Redis，MySQL 里的历史还没人查
  （SQL 已给在 decisions §5）。
- 附件三表（`message_attachment` / `file_object` / `upload_session`）仍 0 行、仍无写入，等发图需求再定。

**跑 e2e 时的两个坑**（会再踩）：

1. 本机 8080 被 WSL 里 SeaweedFS 的 volume 端口占用，`GateServer` 绑 8080 会
   `bind: ... [system:10013]`。用配置文件副本换端口（如 18080）即可。
2. `Set-Content -Encoding utf8` 写出的 ini **带 BOM**，服务端解析报
   `xxx.ini(1): '=' character not found in line`。必须
   `[System.IO.File]::WriteAllLines(path, lines, (New-Object System.Text.UTF8Encoding($false)))`。

演练库 `chatserver_dryrun`（用于演练 012）用完也已删除。
