-- 009 friend_request：timestamp -> datetime、补双向外键
--
-- 问题 1：只有 friend_request 用 TIMESTAMP，其余 8 张表全 DATETIME。
--         TIMESTAMP 按会话时区换算、2038 年溢出；当前 @@time_zone=SYSTEM / CST 看着没事，
--         一旦有客户端 SET time_zone='+00:00'，同一份数据会显示相差 8 小时的两套时间。
--         本机实测转换前后本地时间不变（会话时区即 CST），故直接改类型。
-- 问题 2：applicant_uid / receiver_uid 没有外键，删用户后申请记录变孤儿。
--
-- ON DELETE 选择：CASCADE —— 申请记录是过程性数据，用户没了就该跟着走。
--
-- 回滚：ALTER TABLE `friend_request` MODIFY COLUMN `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP, ...
--       ALTER TABLE `friend_request` DROP FOREIGN KEY `fk_fr_applicant`, DROP FOREIGN KEY `fk_fr_receiver`;

ALTER TABLE `friend_request`
    MODIFY COLUMN `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '首次申请时间（重发只更新 updated_at）',
    MODIFY COLUMN `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON
UPDATE CURRENT_TIMESTAMP COMMENT '最近一次申请/处理时间',
    ADD CONSTRAINT `fk_fr_applicant` FOREIGN KEY (`applicant_uid`) REFERENCES `user` (`uid`)
ON
DELETE
CASCADE,
    ADD CONSTRAINT `fk_fr_receiver` FOREIGN KEY (`receiver_uid`) REFERENCES `user` (`uid`) ON DELETE
CASCADE;

INSERT INTO `schema_migrations` (`version`)
VALUES ('009_friend_request_datetime_fk') ON DUPLICATE KEY
UPDATE `version` = `version`;
