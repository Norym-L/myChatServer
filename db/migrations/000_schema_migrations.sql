-- 000 迁移记账表
-- 目的：让「这个库现在处于第几版」有唯一真相，而不是靠人记。
-- 每个迁移文件末尾都会往这里写一行；db/apply.sh 会跳过已记录的版本。

CREATE TABLE IF NOT EXISTS `schema_migrations`
(
    `version`
    varchar
(
    64
) NOT NULL COMMENT '迁移文件名（不含 .sql）',
    `applied_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '应用时间',
    PRIMARY KEY
(
    `version`
)
    ) ENGINE = InnoDB
    DEFAULT CHARSET = utf8mb4
    COLLATE = utf8mb4_0900_ai_ci
    COMMENT ='已应用的数据库迁移记录';

INSERT INTO `schema_migrations` (`version`)
VALUES ('000_schema_migrations') ON DUPLICATE KEY
UPDATE `version` = `version`;
