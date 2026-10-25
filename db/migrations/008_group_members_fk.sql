-- 008 group_members：补外键 + 群昵称宽度对齐 user.name
--
-- 问题 1：group_members 没有外键。dismiss_group 存储过程里手写「先删成员再删群」，
--         就是在人工替代 ON DELETE CASCADE；少写一句就留孤儿。
-- 问题 2：nickname varchar(50) 与 user.name varchar(64) 宽度不一致，同一套「名字」两条标准。
--
-- ON DELETE 选择：
--   group_id -> CASCADE   群没了，成员关系自然没了（与 dismiss_group 的人工删除等价）
--   user_id  -> RESTRICT  不接受「删用户顺手把群成员删掉」——member_count 是冗余计数，
--                         CASCADE 删成员会让计数进一步漂移；要退群就走存储过程。
--
-- 回滚：ALTER TABLE `group_members` DROP FOREIGN KEY `fk_gm_group`, DROP FOREIGN KEY `fk_gm_user`,
--       MODIFY COLUMN `nickname` varchar(50) DEFAULT NULL;

ALTER TABLE `group_members`
    MODIFY COLUMN `nickname` varchar (64) DEFAULT NULL COMMENT '群内昵称，为空时回落到 user.name',
    ADD CONSTRAINT `fk_gm_group` FOREIGN KEY (`group_id`) REFERENCES `groups` (`id`) ON
DELETE
CASCADE,
    ADD CONSTRAINT `fk_gm_user` FOREIGN KEY (`user_id`) REFERENCES `user` (`uid`) ON DELETE
RESTRICT;

INSERT INTO `schema_migrations` (`version`)
VALUES ('008_group_members_fk') ON DUPLICATE KEY
UPDATE `version` = `version`;
