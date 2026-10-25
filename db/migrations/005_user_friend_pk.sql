-- 005 user_friend：显式主键 + 「成为好友时间」
--
-- 问题 1：user_friend 只有 UNIQUE uk_uid_friend(uid, friend_uid)，没有 PRIMARY KEY 声明。
--         实际是唯一索引在当聚簇索引用 —— 能跑，但表结构上看不出主键是什么，
--         任何 DDL 工具/ORM/复制过滤都会含糊。
-- 问题 2：没有 created_at，UI 想显示「什么时候加的好友」根本查不到。
--
-- 做法：把唯一索引转成同名同列的主键（不是新增冗余索引），顺带加 created_at。
--       两条外键的索引需求仍被满足：uid 由主键最左列覆盖，friend_uid 由 idx_friend_uid 覆盖。
--
-- 回滚：ALTER TABLE `user_friend` DROP PRIMARY KEY, ADD UNIQUE KEY `uk_uid_friend` (`uid`,`friend_uid`),
--       DROP COLUMN `created_at`;

ALTER TABLE `user_friend`
DROP INDEX `uk_uid_friend`,
    ADD PRIMARY KEY (`uid`, `friend_uid`),
    ADD COLUMN `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '成为好友时间（005 之前的老数据为迁移时刻近似值）';

INSERT INTO `schema_migrations` (`version`)
VALUES ('005_user_friend_pk') ON DUPLICATE KEY
UPDATE `version` = `version`;
