-- 001 全库 collation 统一到 utf8mb4_0900_ai_ci
--
-- 问题：user_friend / friend_request 建表时用了 utf8mb4_unicode_ci，其余 7 张表是
--       utf8mb4_0900_ai_ci。两套规则混用，跨表比较字符串直接报错（已实测）：
--         ERROR 1267 (HY000): Illegal mix of collations (utf8mb4_0900_ai_ci,IMPLICIT)
--         and (utf8mb4_unicode_ci,IMPLICIT) for operation '='
--       以后任何「备注 vs 昵称」的比对、ORDER BY 混排、UNION 都会踩到。
--
-- 选择：统一到 8.0 默认的 utf8mb4_0900_ai_ci（不区分大小写与重音，与其余 7 表一致）。
-- 回滚：ALTER TABLE `user_friend` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
--       ALTER TABLE `friend_request` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

ALTER TABLE `user_friend` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
ALTER TABLE `friend_request` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

INSERT INTO `schema_migrations` (`version`)
VALUES ('001_unify_collation') ON DUPLICATE KEY
UPDATE `version` = `version`;
