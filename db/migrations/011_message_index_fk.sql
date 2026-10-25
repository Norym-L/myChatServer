-- 011 message：多态会话索引 + 发送者外键
--
-- ⚠️ 前提说明：这张表目前【0 行且没有任何代码往里写】，聊天记录实际只走 Redis。
--    本迁移是「将来真要落库」时该有的结构修正，不影响现有运行；如果决定删掉这四张表，
--    本迁移可以跳过（见 db/decisions.md 待决事项 #3）。
--
-- 问题 1：session_id 一列同时装「对方 uid」和「group_id」，靠 is_group 区分，
--         但 is_group 没进任何索引 —— 私聊和群聊 id 空间重叠，查一条会话必须额外过滤
--         is_group，索引帮不上忙，还会出现「私聊 5 与群 5 混在同一段索引里」。
--         改成 (is_group, session_id, created_at) 复合索引，drops 旧的 (session_id, created_at)。
-- 问题 2：sender_id 没有外键。
-- 问题 3：status 列没有注释；本项目的状态语义文档里也没写，故只写「未定义」，不臆造。
--
-- 回滚：ALTER TABLE `message` DROP FOREIGN KEY `fk_msg_sender`, DROP INDEX `idx_session`,
--       ADD KEY `idx_session_time` (`session_id`,`created_at`);

ALTER TABLE `message`
    MODIFY COLUMN `is_group` tinyint NOT NULL DEFAULT 0 COMMENT '0私聊 / 1群聊（决定 session_id 指向 user.uid 还是 groups.id）',
    MODIFY COLUMN `msg_type` tinyint NOT NULL DEFAULT 0 COMMENT '消息类型（语义未定义，待补）',
    MODIFY COLUMN `status` tinyint NOT NULL DEFAULT 0 COMMENT '消息状态（语义未定义，待补）',
DROP INDEX `idx_session_time`,
    ADD KEY `idx_session` (`is_group`, `session_id`, `created_at`),
    ADD CONSTRAINT `fk_msg_sender` FOREIGN KEY (`sender_id`) REFERENCES `user` (`uid`) ON
DELETE
RESTRICT;

INSERT INTO `schema_migrations` (`version`)
VALUES ('011_message_index_fk') ON DUPLICATE KEY
UPDATE `version` = `version`;
