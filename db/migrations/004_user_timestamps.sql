-- 004 user 补时间戳
--
-- 问题：user 表没有 created_at，注册时间完全查不到；也没有 updated_at，改过什么无从追溯。
-- 注意：本次迁移之前已存在的行，created_at 只能记为「迁移时刻」（历史值已丢失），
--       这 13 行的注册时间是不可信的近似值，不要拿来做统计。
--
-- 回滚：ALTER TABLE `user` DROP COLUMN `updated_at`, DROP COLUMN `created_at`;

ALTER TABLE `user`
    ADD COLUMN `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间（004 之前的老数据为迁移时刻近似值）' AFTER `sex`,
    ADD COLUMN `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON
UPDATE CURRENT_TIMESTAMP COMMENT '最后修改时间' AFTER `created_at`;

INSERT INTO `schema_migrations` (`version`)
VALUES ('004_user_timestamps') ON DUPLICATE KEY
UPDATE `version` = `version`;
