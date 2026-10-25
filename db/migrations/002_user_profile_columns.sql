-- 002 user 资料列：`desc` -> `bio`、icon 4096 -> 512
--
-- 问题 1：DESC 是 MySQL 保留字。实测 `SELECT desc FROM chatserver.user LIMIT 1` 直接
--         ERROR 1064 (42000): You have an error in your SQL syntax。
--         现在能跑只因为服务端处处加了反引号，任何手写 SQL / BI 工具都会踩。
-- 问题 2：icon varchar(4096) 在 utf8mb4 下单行最多占 16KB，而 groups.avatar_url 只有
--         512，同一个「头像」两条标准。若有人往 icon 塞 base64，user 表体积与备份立即失控。
--
-- 影响面（已同步修改，本次一起提交）：
--   src/public/sql/mysql/MySqlDao.cpp:311,358      SELECT `desc` -> `bio`
--   src/public/sql/mysql/Mysql_boost.cpp:173,216   SELECT `desc` -> `bio`
--   存储过程 update_user_info                      见本文件末尾重建（形参 p_desc -> p_bio，位置不变）
--   JSON 字段名保持 "desc" 不变，客户端无需改动。
--
-- 回滚：ALTER TABLE `user` CHANGE COLUMN `bio` `desc` varchar(256) DEFAULT NULL; 并重建过程。

ALTER TABLE `user`
    CHANGE COLUMN `desc` `bio` varchar (256) DEFAULT NULL COMMENT '个性签名',
    MODIFY COLUMN `icon` varchar (512) DEFAULT NULL COMMENT '头像 object_key / URL（不存 base64）';

DROP PROCEDURE IF EXISTS `update_user_info`;
DELIMITER;;
CREATE PROCEDURE `update_user_info`(IN p_uid bigint unsigned, IN p_name varchar (64),
                                    IN p_bio varchar (256), IN p_icon varchar (512),
                                    IN p_sex tinyint, OUT p_error_code int)
BEGIN
    DECLARE
v_user_count INT DEFAULT 0;

    DECLARE
EXIT HANDLER FOR SQLEXCEPTION
BEGIN
            SET
p_error_code = -10; -- -10 代表数据库异常
END;

    SET
p_error_code = 0;

    #
1. 检查用户是否存在
SELECT COUNT(*)
INTO v_user_count
FROM `user`
WHERE `uid` = p_uid;

IF
v_user_count = 0 THEN
        SET p_error_code = -1; #
-1 用户不存在
    ELSE
UPDATE `user`
SET `name` = IFNULL(NULLIF(p_name, ''), `name`),
    `bio`  = IFNULL(p_bio, `bio`),
    `icon` = IFNULL(p_icon, `icon`),
    `sex`  = IFNULL(p_sex, `sex`)
WHERE `uid` = p_uid;

SET
p_error_code = 0;
END IF;
END ;;
DELIMITER ;

INSERT INTO `schema_migrations` (`version`)
VALUES ('002_user_profile_columns') ON DUPLICATE KEY
UPDATE `version` = `version`;
