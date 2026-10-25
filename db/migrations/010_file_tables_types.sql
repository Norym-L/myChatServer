-- 010 文件四表：UUID 用 ascii、有符号 -> 无符号、状态列注释
--
-- 问题 1：file_object.id 是 char(36) 的 UUID，建在 utf8mb4 下 —— 每个键占 36*4=144 字节，
--         而它被 message_attachment.file_id（主键第二列 + 二级索引 + 外键）和
--         upload_session.file_id 一起引用，同一串 UUID 在三处各膨胀 4 倍。
--         改成 ascii/ascii_bin 后每处 36 字节（实测 36*4=144 vs 36*1=36）。
-- 问题 2：file_size 是【有符号】bigint（能存负数），upload_session.id 也是有符号，
--         而其余 7 张表的 id 全是 bigint unsigned。
-- 问题 3：status 列没有注释，0/1/2 各代表什么只能翻代码。
--
-- 注意：改外键列的类型必须先丢掉外键再改，否则 ERROR 1832/1833；四张表当前都是 0 行，
--       顺序安全。外键语义保持不变（RESTRICT）。
--
-- 回滚：先 DROP 两个外键，把 id/file_id 改回 char(36) 默认字符集，再重建外键。

ALTER TABLE `message_attachment` DROP FOREIGN KEY `fk_attach_file`;
ALTER TABLE `upload_session` DROP FOREIGN KEY `fk_session_file`;

ALTER TABLE `file_object`
    MODIFY COLUMN `id` char (36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT 'UUIDv4（ascii 存储，36 字节）',
    MODIFY COLUMN `file_size` bigint unsigned NOT NULL DEFAULT 0 COMMENT '文件字节数',
    MODIFY COLUMN `status` tinyint NOT NULL DEFAULT 0 COMMENT '0待上传/1可用/2已删除';

ALTER TABLE `message_attachment`
    MODIFY COLUMN `file_id` char (36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT '引用 file_object.id';

ALTER TABLE `upload_session`
    MODIFY COLUMN `id` bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '上传会话 id',
    MODIFY COLUMN `file_id` char (36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT '引用 file_object.id',
    MODIFY COLUMN `status` tinyint NOT NULL DEFAULT 0 COMMENT '0待确认/1已确认/2已过期';

ALTER TABLE `message_attachment`
    ADD CONSTRAINT `fk_attach_file` FOREIGN KEY (`file_id`) REFERENCES `file_object` (`id`) ON DELETE RESTRICT;
ALTER TABLE `upload_session`
    ADD CONSTRAINT `fk_session_file` FOREIGN KEY (`file_id`) REFERENCES `file_object` (`id`) ON DELETE RESTRICT;

INSERT INTO `schema_migrations` (`version`)
VALUES ('010_file_tables_types') ON DUPLICATE KEY
UPDATE `version` = `version`;
