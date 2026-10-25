-- 007 groups：补 owner 外键、成员数 CHECK、群名索引
--
-- 问题 1：groups.owner_id 没有外键，群主被删/写错就是孤儿。
-- 问题 2：member_count 与 max_members 没有任何约束，计数可以大于上限。
-- 问题 3：groups.name 上没有任何索引，「搜索群」只能全表扫。
--
-- ON DELETE 选择：RESTRICT（不级联）。理由：群主删号必须先显式转移/解散群，
-- 静默 CASCADE 会把整个群连带成员一起删掉，代价太大。
-- 注意 CHECK(member_count <= max_members) 依赖 006 的计数修复，顺序不可颠倒。
--
-- 回滚：ALTER TABLE `groups` DROP FOREIGN KEY `fk_groups_owner`,
--       DROP CHECK `ck_groups_member_count`, DROP INDEX `idx_name`;

ALTER TABLE `groups`
    ADD CONSTRAINT `fk_groups_owner` FOREIGN KEY (`owner_id`) REFERENCES `user` (`uid`) ON DELETE RESTRICT,
    ADD CONSTRAINT `ck_groups_member_count` CHECK (`member_count` <= `max_members`),
    ADD KEY `idx_name` (`name`);

INSERT INTO `schema_migrations` (`version`)
VALUES ('007_groups_constraints') ON DUPLICATE KEY
UPDATE `version` = `version`;
