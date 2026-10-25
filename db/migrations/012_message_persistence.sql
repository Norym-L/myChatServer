-- 012_message_persistence.sql
-- 目的：让聊天消息真正落库。此前 `message` 表建了但 0 行，聊天记录只在 Redis，
--       现在 `sendTextMessageHandler`（src/chatServer/LogicSystem.cpp）会在投递成功后写一条历史。
--
-- 为什么这么改：
--   1) `msg_type` / `status` 原来注释是「语义未定义」。现在明确：msg_type 对齐
--      `chat_message::Type`（1文本/2图片/3文件/4表情包）；status 只表示「这行已入库存档」，
--      在线投递、离线拉取、ACK 全部仍由 Redis 负责（Redis 才是投递真相，MySQL 是历史真相）。
--   2) 加 `client_msg_id`：客户端每条消息自带 `msg_id`（LogicSystem 里会原样回给客户端）。
--      网络抖动重试时同一条消息会重复到达，唯一键 `(sender_id, client_msg_id)` 让它幂等。
--      客户端没带 msg_id 时写 NULL —— 唯一键允许多个 NULL，不会被误判成重复。
--   3) 加 `conversation_id` 生成列：`session_id` 是多态的（单聊=对方 uid、群聊=群 id），
--      直接把多态塞进索引会导致会话历史只能这样查：
--          WHERE (sender_id=? AND session_id=?) OR (sender_id=? AND session_id=?)
--      OR 跨列，索引基本用不上。生成列把两种方向归一成一个键：
--          群聊  g<群id>              例：g3
--          单聊  u<小uid>:<大uid>     例：u1001:666672
--      这样一次范围扫描就能按 id 倒序翻页（索引 `idx_conversation(conversation_id, id)`）。
--      用 STORED 而不是在 C++ 里拼：任何写入方（含以后加的存储过程）都不会忘了维护它。
--   4) 索引收敛：`uk_sender_client` 的左前缀就是 `sender_id`，`idx_sender` 冗余，删；
--      `idx_conversation` 覆盖会话分页，`idx_session` 冗余，删。消息是最高频写入，
--      少两个索引就少两份写放大。
--
-- 为什么不写存储过程：其余业务写操作都走 CALL，但这个函数在每条消息上都要跑一次；
--   `callMysqlSp*` 的固定成本是「CALL + 再查一次 @error」两次往返。消息落库用一条
--   预处理 INSERT（ON DUPLICATE KEY UPDATE id=id）就够了，一次往返，见 Mysql_boost::saveMessage。
--
-- 回滚：
--   ALTER TABLE `message`
--       DROP INDEX `uk_sender_client`, DROP INDEX `idx_conversation`,
--       DROP COLUMN `conversation_id`, DROP COLUMN `client_msg_id`,
--       ADD KEY `idx_sender` (`sender_id`),
--       ADD KEY `idx_session` (`is_group`, `session_id`, `created_at`);
--   然后把 msg_type / status / content 的 COMMENT 改回「语义未定义，待补」。

-- 1) 列语义 + 新列
ALTER TABLE `message`
    MODIFY COLUMN `msg_type` tinyint NOT NULL DEFAULT 0 COMMENT '1文本/2图片/3文件/4表情包（对齐 chat_message::Type；0=未知）',
    MODIFY COLUMN `status` tinyint NOT NULL DEFAULT 0 COMMENT '0=已入库存档；在线投递/离线拉取/ACK 由 Redis 管理，本列不参与业务',
    MODIFY COLUMN `content` text NULL COMMENT '消息体：文本为正文；图片/文件/表情为引用信息（object_key 或 JSON）',
    ADD COLUMN `client_msg_id` varchar (64) NULL COMMENT '客户端消息 id（msg_id），同一发送者重试时用于幂等' AFTER `content`,
    ADD COLUMN `conversation_id` varchar (64)
    GENERATED ALWAYS AS (
    IF(`is_group` = 1,
    CONCAT('g', `session_id`),
    CONCAT('u', LEAST(`sender_id`, `session_id`), ':', GREATEST(`sender_id`, `session_id`)))
    ) STORED COMMENT '会话键：群=g<群id>；单聊=u<小uid>:<大uid>（生成列，供历史分页）';

-- 2) 索引：新增去重键与会话分页键
ALTER TABLE `message`
    ADD UNIQUE KEY `uk_sender_client` (`sender_id`, `client_msg_id`),
    ADD KEY `idx_conversation` (`conversation_id`, `id`);

-- 3) 删冗余索引（放到最后，避免同一句 ALTER 里外键找不到可用索引）
ALTER TABLE `message`
DROP
KEY `idx_session`,
    DROP
KEY `idx_sender`;

INSERT INTO `schema_migrations` (`version`)
VALUES ('012_message_persistence') ON DUPLICATE KEY
UPDATE `version` = `version`;
