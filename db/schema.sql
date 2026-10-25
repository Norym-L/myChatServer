/*!40101 SET @OLD_CHARACTER_SET_CLIENT = @@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS = @@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION = @@COLLATION_CONNECTION */;
/*!50503 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE = @@TIME_ZONE */;
/*!40103 SET TIME_ZONE = '+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS = @@UNIQUE_CHECKS, UNIQUE_CHECKS = 0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS = @@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS = 0 */;
/*!40101 SET @OLD_SQL_MODE = @@SQL_MODE, SQL_MODE = 'NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES = @@SQL_NOTES, SQL_NOTES = 0 */;
DROP TABLE IF EXISTS `file_object`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `file_object`
(
    `id`            char(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT 'UUIDv4（ascii 存储，36 字节）',
    `object_key`    varchar(512)                                   NOT NULL,
    `file_hash`     varchar(64)                                    NOT NULL,
    `file_size`     bigint unsigned                                NOT NULL DEFAULT '0' COMMENT '文件字节数',
    `mime_type`     varchar(128)                                            DEFAULT NULL,
    `original_name` varchar(255)                                   NOT NULL,
    `owner_id`      bigint unsigned                                NOT NULL,
    `status`        tinyint                                        NOT NULL DEFAULT '0' COMMENT '0待上传/1可用/2已删除',
    `created_at`    datetime                                       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_object_key` (`object_key`),
    KEY `idx_file_hash_status` (`file_hash`, `status`),
    KEY `idx_owner_status` (`owner_id`, `status`),
    CONSTRAINT `fk_file_owner` FOREIGN KEY (`owner_id`) REFERENCES `user` (`uid`)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `friend_request`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `friend_request`
(
    `id`            bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `applicant_uid` bigint unsigned NOT NULL COMMENT '申请人uid',
    `receiver_uid`  bigint unsigned NOT NULL COMMENT '接收人uid',
    `message`       varchar(256)             DEFAULT NULL COMMENT '申请附言/验证消息',
    `status`        tinyint         NOT NULL DEFAULT '0' COMMENT '状态: 0-待处理, 1-已同意, 2-已拒绝',
    `created_at`    datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '首次申请时间（重发只更新 updated_at）',
    `updated_at`    datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最近一次申请/处理时间',
    `remark`        varchar(256)             DEFAULT NULL COMMENT '给对方的备注',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_applicant_receiver` (`applicant_uid`, `receiver_uid`),
    KEY `idx_receiver_status` (`receiver_uid`, `status`),
    CONSTRAINT `fk_fr_applicant` FOREIGN KEY (`applicant_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
    CONSTRAINT `fk_fr_receiver` FOREIGN KEY (`receiver_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE
) ENGINE = InnoDB
  AUTO_INCREMENT = 43
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci COMMENT ='好友申请表';
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `group_members`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `group_members`
(
    `group_id`  bigint unsigned                 NOT NULL,
    `user_id`   bigint unsigned                 NOT NULL,
    `role`      enum ('owner','admin','member') NOT NULL DEFAULT 'member',
    `nickname`  varchar(64)                              DEFAULT NULL COMMENT '群内昵称，为空时回落到 user.name',
    `joined_at` datetime                        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`group_id`, `user_id`),
    KEY `idx_user_group` (`user_id`, `group_id`),
    KEY `idx_group_role` (`group_id`, `role`),
    CONSTRAINT `fk_gm_group` FOREIGN KEY (`group_id`) REFERENCES `groups` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_gm_user` FOREIGN KEY (`user_id`) REFERENCES `user` (`uid`) ON DELETE RESTRICT
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `groups`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `groups`
(
    `id`           bigint unsigned NOT NULL AUTO_INCREMENT,
    `name`         varchar(150)    NOT NULL COMMENT '群名称',
    `avatar_url`   varchar(512)             DEFAULT NULL COMMENT '群头像',
    `owner_id`     bigint unsigned NOT NULL COMMENT '群主id',
    `announcement` text COMMENT '群公告',
    `max_members`  int unsigned    NOT NULL DEFAULT '500' COMMENT '最大用户数量',
    `member_count` int unsigned    NOT NULL DEFAULT '0',
    `created_at`   datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at`   datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_owner` (`owner_id`),
    KEY `idx_name` (`name`),
    CONSTRAINT `fk_groups_owner` FOREIGN KEY (`owner_id`) REFERENCES `user` (`uid`) ON DELETE RESTRICT,
    CONSTRAINT `ck_groups_member_count` CHECK ((`member_count` <= `max_members`))
) ENGINE = InnoDB
  AUTO_INCREMENT = 6
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `message`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `message`
(
    `id`              bigint unsigned NOT NULL AUTO_INCREMENT,
    `sender_id`       bigint unsigned NOT NULL,
    `session_id`      bigint unsigned NOT NULL,
    `is_group`        tinyint         NOT NULL DEFAULT '0' COMMENT '0私聊 / 1群聊（决定 session_id 指向 user.uid 还是 groups.id）',
    `msg_type`        tinyint         NOT NULL DEFAULT '0' COMMENT '1文本/2图片/3文件/4表情包（对齐 chat_message::Type；0=未知）',
    `content`         text COMMENT '消息体：文本为正文；图片/文件/表情为引用信息（object_key 或 JSON）',
    `client_msg_id`   varchar(64)              DEFAULT NULL COMMENT '客户端消息 id（msg_id），同一发送者重试时用于幂等',
    `created_at`      datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `status`          tinyint         NOT NULL DEFAULT '0' COMMENT '0=已入库存档；在线投递/离线拉取/ACK 由 Redis 管理，本列不参与业务',
    `conversation_id` varchar(64) GENERATED ALWAYS AS (if((`is_group` = 1), concat(_utf8mb4'g', `session_id`),
                                                          concat(_utf8mb4'u', least(`sender_id`, `session_id`),
                                                                 _utf8mb4':',
                                                                 greatest(`sender_id`, `session_id`)))) STORED COMMENT '会话键：群=g<群id>；单聊=u<小uid>:<大uid>（生成列，供历史分页）',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_sender_client` (`sender_id`, `client_msg_id`),
    KEY `idx_conversation` (`conversation_id`, `id`),
    CONSTRAINT `fk_msg_sender` FOREIGN KEY (`sender_id`) REFERENCES `user` (`uid`) ON DELETE RESTRICT
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `message_attachment`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `message_attachment`
(
    `message_id` bigint unsigned                                NOT NULL,
    `file_id`    char(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT '引用 file_object.id',
    `sort_order` int                                            NOT NULL DEFAULT '0',
    PRIMARY KEY (`message_id`, `file_id`),
    KEY `idx_file_id` (`file_id`),
    CONSTRAINT `fk_attach_file` FOREIGN KEY (`file_id`) REFERENCES `file_object` (`id`) ON DELETE RESTRICT,
    CONSTRAINT `fk_attach_message` FOREIGN KEY (`message_id`) REFERENCES `message` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `schema_migrations`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `schema_migrations`
(
    `version`    varchar(64) NOT NULL COMMENT '迁移文件名（不含 .sql）',
    `applied_at` datetime    NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '应用时间',
    PRIMARY KEY (`version`)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci COMMENT ='已应用的数据库迁移记录';
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `upload_session`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `upload_session`
(
    `id`         bigint unsigned                                NOT NULL AUTO_INCREMENT COMMENT '上传会话 id',
    `file_id`    char(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL COMMENT '引用 file_object.id',
    `expires_at` datetime                                       NOT NULL,
    `status`     tinyint                                        NOT NULL DEFAULT '0' COMMENT '0待确认/1已确认/2已过期',
    `created_at` datetime                                       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_file_id` (`file_id`),
    KEY `idx_status_expiry` (`status`, `expires_at`),
    CONSTRAINT `fk_session_file` FOREIGN KEY (`file_id`) REFERENCES `file_object` (`id`) ON DELETE RESTRICT
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `user`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `user`
(
    `uid`        bigint unsigned NOT NULL AUTO_INCREMENT COMMENT '用户账号',
    `name`       varchar(64)     NOT NULL COMMENT '用户名',
    `email`      varchar(255)    NOT NULL COMMENT '邮箱',
    `password`   varchar(255)    NOT NULL COMMENT '用户密码（加密）',
    `bio`        varchar(256)             DEFAULT NULL COMMENT '个性签名',
    `icon`       varchar(512)             DEFAULT NULL COMMENT '头像 object_key / URL（不存 base64）',
    `sex`        tinyint         NOT NULL DEFAULT '2' COMMENT '0男/1女/2未知（对齐 enum Sex）',
    `created_at` datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '注册时间（004 之前的老数据为迁移时刻近似值）',
    `updated_at` datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后修改时间',
    PRIMARY KEY (`uid`),
    CONSTRAINT `ck_user_sex` CHECK ((`sex` in (0, 1, 2)))
) ENGINE = InnoDB
  AUTO_INCREMENT = 666685
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci COMMENT ='用户信息';
/*!40101 SET character_set_client = @saved_cs_client */;
DROP TABLE IF EXISTS `user_friend`;
/*!40101 SET @saved_cs_client = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE `user_friend`
(
    `uid`        bigint unsigned NOT NULL COMMENT '用户id',
    `friend_uid` bigint unsigned NOT NULL COMMENT '好友id',
    `remark`     varchar(256)             DEFAULT NULL COMMENT '好友备注',
    `created_at` datetime        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '成为好友时间（005 之前的老数据为迁移时刻近似值）',
    PRIMARY KEY (`uid`, `friend_uid`),
    KEY `idx_friend_uid` (`friend_uid`),
    CONSTRAINT `user_friend_user_friend_uid_fk` FOREIGN KEY (`friend_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
    CONSTRAINT `user_friend_user_uid_fk` FOREIGN KEY (`uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci COMMENT ='用户好友列表';
/*!40101 SET character_set_client = @saved_cs_client */;
/*!50003 DROP PROCEDURE IF EXISTS `add_friend` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `add_friend`(IN p_uid bigint unsigned, IN p_friend_uid bigint unsigned,
                                                        IN p_remark varchar(256), OUT p_result int)
BEGIN
    DECLARE v_user_count INT DEFAULT 0;
    DECLARE v_friend_count INT DEFAULT 0;
    DECLARE v_friend_exist INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 校验是否添加自己
    IF p_uid = p_friend_uid THEN
        SET p_result = -3; # -3 代表不能添加自己
    ELSE
        # 2. 检查双方用户是否存在
        SELECT COUNT(*) INTO v_user_count FROM `user` WHERE `uid` = p_uid;
        SELECT COUNT(*) INTO v_friend_count FROM `user` WHERE `uid` = p_friend_uid;

        IF v_user_count = 0 OR v_friend_count = 0 THEN
            SET p_result = -1; # -1 代表用户不存在
        ELSE
            # 3. 检查是否已经是好友
            SELECT COUNT(*) INTO v_friend_exist FROM `user_friend` WHERE `uid` = p_uid AND `friend_uid` = p_friend_uid;

            IF v_friend_exist > 0 THEN
                SET p_result = -2; # -2 代表已经是好友关系
            ELSE
                # 4. 双向插入记录（利用事务保证原子性）
                START TRANSACTION;

                INSERT INTO `user_friend` (`uid`, `friend_uid`, `remark`)
                VALUES (p_uid, p_friend_uid, p_remark);

                INSERT INTO `user_friend` (`uid`, `friend_uid`, `remark`)
                VALUES (p_friend_uid, p_uid, '');
                COMMIT;

                SET p_result = 0; # 0 代表添加成功
            END IF;
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `add_group_member` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `add_group_member`(IN p_group_id bigint unsigned, IN p_uid bigint unsigned,
                                                              IN p_operator_id bigint unsigned, OUT p_result int)
BEGIN
    DECLARE v_group_exist INT DEFAULT 0;
    DECLARE v_operator_role VARCHAR(10) DEFAULT '';
    DECLARE v_member_exist INT DEFAULT 0;
    DECLARE v_update_rows INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            ROLLBACK;
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 检查群是否存在
    SELECT COUNT(*) INTO v_group_exist FROM `groups` WHERE `id` = p_group_id;

    IF v_group_exist = 0 THEN
        SET p_result = -1; # -1 代表群不存在
    ELSE
        # 2. 检查操作人是否是群主或管理员
        SELECT `role`
        INTO v_operator_role
        FROM `group_members`
        WHERE `group_id` = p_group_id AND `user_id` = p_operator_id;

        IF v_operator_role != 'owner' AND v_operator_role != 'admin' THEN
            SET p_result = -2; # -2 代表操作人权限不足
        ELSE
            # 3. 检查目标用户是否已经在群里
            SELECT COUNT(*)
            INTO v_member_exist
            FROM `group_members`
            WHERE `group_id` = p_group_id AND `user_id` = p_uid;

            IF v_member_exist > 0 THEN
                SET p_result = -3; # -3 代表用户已在群内
            ELSE
                # 4. 开启事务，尝试更新群人数（防止并发超员）
                START TRANSACTION;

                UPDATE `groups`
                SET `member_count` = `member_count` + 1
                WHERE `id` = p_group_id
                  AND `member_count` < `max_members`;

                SET v_update_rows = ROW_COUNT();

                IF v_update_rows = 0 THEN
                    ROLLBACK;
                    SET p_result = -4; # -4 代表群人数已满
                ELSE
                    # 5. 插入成员记录
                    INSERT INTO `group_members` (`group_id`, `user_id`, `role`)
                    VALUES (p_group_id, p_uid, 'member');

                    COMMIT;

                    SET p_result = 0; # 0 代表添加成功
                END IF;
            END IF;
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `create_group` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `create_group`(IN p_name varchar(150), IN p_avatar_url varchar(512),
                                                          IN p_owner_id bigint unsigned, IN p_max_members int unsigned,
                                                          IN p_announcement text, OUT p_group_id bigint unsigned,
                                                          OUT p_result int)
BEGIN
    DECLARE v_user_count INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            ROLLBACK;
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 检查群主用户是否存在
    SELECT COUNT(*) INTO v_user_count FROM `user` WHERE `uid` = p_owner_id;

    IF v_user_count = 0 THEN
        SET p_result = -1; # -1 代表用户不存在
    ELSE
        # 2. 开启事务，新建群聊
        START TRANSACTION;

        INSERT INTO `groups` (`name`, `avatar_url`, `owner_id`, `max_members`, `member_count`, `announcement`)
        VALUES (p_name, p_avatar_url, p_owner_id, p_max_members, 1, p_announcement);

        SET p_group_id = LAST_INSERT_ID();

        # 3. 将群主加入群成员表
        INSERT INTO `group_members` (`group_id`, `user_id`, `role`)
        VALUES (p_group_id, p_owner_id, 'owner');
        COMMIT;

        SET p_result = 0; # 0 代表新建成功
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `del_friend` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `del_friend`(IN p_uid bigint unsigned, IN p_friend_uid bigint unsigned,
                                                        OUT p_result int)
BEGIN
    DECLARE v_friend_exist INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 检查好友关系是否存在
    SELECT COUNT(*)
    INTO v_friend_exist
    FROM `user_friend`
    WHERE `uid` = p_uid
      AND `friend_uid` = p_friend_uid;

    IF v_friend_exist = 0 THEN
        SET p_result = -1; # -1 代表好友关系不存在
    ELSE
        # 2. 双向删除记录（使用事务保证原子性）
        START TRANSACTION;

        DELETE
        FROM `user_friend`
        WHERE (`uid` = p_uid AND `friend_uid` = p_friend_uid)
           OR (`uid` = p_friend_uid AND `friend_uid` = p_uid);
        COMMIT;

        SET p_result = 0; # 0 代表删除成功
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `dismiss_group` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `dismiss_group`(IN p_group_id bigint unsigned,
                                                           IN p_operator_id bigint unsigned,
                                                           OUT p_result int)
BEGIN
    DECLARE v_group_owner_id BIGINT UNSIGNED DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            ROLLBACK;
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 检查群是否存在，并获取群主ID
    SELECT `owner_id` INTO v_group_owner_id FROM `groups` WHERE `id` = p_group_id;

    IF v_group_owner_id = 0 THEN
        SET p_result = -1; # -1 代表群不存在
    ELSE
        # 2. 检查操作人是否是群主
        IF v_group_owner_id != p_operator_id THEN
            SET p_result = -2; # -2 代表权限不足（只有群主能解散）
        ELSE
            # 3. 开启事务，先删除群成员，再删除群
            START TRANSACTION;

            DELETE FROM `group_members` WHERE `group_id` = p_group_id;
            DELETE FROM `groups` WHERE `id` = p_group_id;
            COMMIT;

            SET p_result = 0; # 0 代表解散成功
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `get_request_sender_uid` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `get_request_sender_uid`(
    IN p_request_id BIGINT UNSIGNED,
    OUT p_sender_id BIGINT UNSIGNED,
    OUT p_error_code INT
)
BEGIN
    DECLARE v_not_found BOOLEAN DEFAULT FALSE;

    -- 未查询到记录时（NOT FOUND）不会抛异常，这里单独捕获
    DECLARE CONTINUE HANDLER FOR NOT FOUND
        SET v_not_found = TRUE;

    -- 数据库异常：参照 handle_friend_request 返回 -10
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_sender_id = NULL;
            SET p_error_code = -10;
        END;

    -- OUT 参数默认就是 NULL，这里显式赋值，语义更清晰
    SET p_sender_id = NULL;
    SET p_error_code = 0;

    SELECT `applicant_uid`
    INTO p_sender_id
    FROM `friend_request`
    WHERE `id` = p_request_id
    LIMIT 1;

    IF v_not_found OR p_sender_id IS NULL THEN
        SET p_sender_id = NULL;
        SET p_error_code = -1; -- -1 表示申请不存在
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `handle_friend_request` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `handle_friend_request`(IN p_request_id bigint unsigned,
                                                                   IN p_receiver_uid bigint unsigned,
                                                                   IN p_status tinyint,
                                                                   OUT p_sender_id bigint unsigned,
                                                                   OUT p_error_code int)
BEGIN
    DECLARE v_applicant_uid BIGINT UNSIGNED;
    DECLARE v_req_status TINYINT DEFAULT -1;
    DECLARE v_remark VARCHAR(256) DEFAULT '';
    DECLARE v_not_found BOOLEAN DEFAULT FALSE;

    -- SELECT ... INTO 无匹配行时只触发 NOT FOUND，不会进 SQLEXCEPTION
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            ROLLBACK;
            SET p_sender_id = v_applicant_uid; -- 已查到时回传，未查到则为 NULL
            SET p_error_code = -10;
        END;

    SET p_sender_id = 0;
    SET p_error_code = 0;

    -- 事务提前开启，保证下面 SELECT ... FOR UPDATE 的行锁真正生效
    START TRANSACTION;

    SELECT `applicant_uid`, `status`, `remark`
    INTO v_applicant_uid, v_req_status, v_remark
    FROM `friend_request`
    WHERE `id` = p_request_id
      AND `receiver_uid` = p_receiver_uid
        FOR
    UPDATE;

    IF v_not_found OR v_applicant_uid IS NULL THEN
        SET p_sender_id = NULL;
        SET p_error_code = -1; -- 申请不存在或无权处理
        ROLLBACK; -- 结束事务、释放锁
    ELSEIF v_req_status <> 0 THEN
        SET p_sender_id = v_applicant_uid;
        SET p_error_code = -2; -- 该申请已被处理过
        ROLLBACK;
    ELSE
        SET p_sender_id = v_applicant_uid;

        UPDATE `friend_request` SET `status` = p_status WHERE `id` = p_request_id;

        IF p_status = 1 THEN
            INSERT IGNORE INTO `user_friend` (`uid`, `friend_uid`, `remark`)
            VALUES (v_applicant_uid, p_receiver_uid, v_remark);

            INSERT IGNORE INTO `user_friend` (`uid`, `friend_uid`, `remark`)
            VALUES (p_receiver_uid, v_applicant_uid, '');

            UPDATE `friend_request`
            SET `status` = 1
            WHERE `applicant_uid` = p_receiver_uid
              AND `receiver_uid` = v_applicant_uid
              AND `status` = 0;
        END IF;

        COMMIT;
        SET p_error_code = 0; -- 处理成功
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `kick_group_member` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `kick_group_member`(IN p_group_id bigint unsigned, IN p_uid bigint unsigned,
                                                               IN p_operator_id bigint unsigned, OUT p_result int)
BEGIN
    DECLARE v_group_owner_id BIGINT UNSIGNED DEFAULT 0;
    DECLARE v_operator_role VARCHAR(10) DEFAULT '';
    DECLARE v_target_role VARCHAR(10) DEFAULT '';

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            ROLLBACK;
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 检查群是否存在，并获取群主ID
    SELECT `owner_id` INTO v_group_owner_id FROM `groups` WHERE `id` = p_group_id;

    IF v_group_owner_id = 0 THEN
        SET p_result = -1; # -1 代表群不存在
    ELSE
        # 2. 检查目标用户是否是群主 (群主不能被踢)
        IF p_uid = v_group_owner_id THEN
            SET p_result = -2; # -2 代表不能踢出群主
        ELSE
            # 3. 检查目标用户是否在群里，并获取其角色
            SELECT `role` INTO v_target_role FROM `group_members` WHERE `group_id` = p_group_id AND `user_id` = p_uid;

            IF v_target_role = '' THEN
                SET p_result = -3; # -3 代表目标用户不在群内
            ELSE
                # 4. 如果操作人不是自己（退群），则需要校验操作人权限
                IF p_operator_id != p_uid THEN
                    SELECT `role`
                    INTO v_operator_role
                    FROM `group_members`
                    WHERE `group_id` = p_group_id AND `user_id` = p_operator_id;

                    IF v_operator_role != 'owner' AND v_operator_role != 'admin' THEN
                        SET p_result = -4; # -4 代表操作人权限不足
                    ELSEIF v_operator_role = 'admin' AND v_target_role = 'admin' THEN
                        SET p_result = -5; # -5 代表管理员不能互踢
                    ELSE
                        # 开启事务，执行踢出操作
                        START TRANSACTION;

                        DELETE FROM `group_members` WHERE `group_id` = p_group_id AND `user_id` = p_uid;
                        UPDATE `groups` SET `member_count` = `member_count` - 1 WHERE `id` = p_group_id;
                        COMMIT;

                        SET p_result = 0; # 0 代表踢出成功
                    END IF;
                ELSE
                    # 用户自己主动退群
                    START TRANSACTION;

                    DELETE FROM `group_members` WHERE `group_id` = p_group_id AND `user_id` = p_uid;
                    UPDATE `groups` SET `member_count` = `member_count` - 1 WHERE `id` = p_group_id;
                    COMMIT;

                    SET p_result = 0; # 0 代表退群成功
                END IF;
            END IF;
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `reg_user` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `reg_user`(IN p_name varchar(64), IN p_email varchar(255),
                                                      IN p_password varchar(255), OUT p_uid bigint unsigned,
                                                      OUT p_error int)
BEGIN
    DECLARE v_exist INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_uid = 0;
            SET p_error = -10; -- -10 代表数据库异常
        END;

    SELECT COUNT(*) INTO v_exist FROM `user` WHERE `email` = p_email;

    # 一个邮箱之多创建5个账号
    IF v_exist > 5 THEN
        SET p_error = -1;
    ELSE
        INSERT INTO `user` (`name`, `email`, `password`)
        VALUES (p_name, p_email, p_password);
        SET p_uid = LAST_INSERT_ID();
        SET p_error = 0;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `send_friend_request` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `send_friend_request`(IN p_applicant_uid BIGINT UNSIGNED,
                                                                 IN p_receiver_uid BIGINT UNSIGNED,
                                                                 IN p_message VARCHAR(256),
                                                                 IN p_remark VARCHAR(256),
                                                                 OUT p_request_id BIGINT UNSIGNED,
                                                                 OUT p_error_code INT)
BEGIN
    DECLARE v_user_count INT DEFAULT 0;
    DECLARE v_is_friend INT DEFAULT 0;
    DECLARE v_is_reverse INT DEFAULT 0;
    DECLARE v_exist_req INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_request_id = 0;
            SET p_error_code = -10; -- -10 代表数据库异常
        END;

    SET p_request_id = 0;
    SET p_error_code = 0;

    # 1. 校验是否添加自己
    IF p_applicant_uid = p_receiver_uid THEN
        SET p_request_id = 0;
        SET p_error_code = -3; # -3 不能添加自己
    ELSE
        # 2. 检查双方用户是否存在
        SELECT COUNT(*) INTO v_user_count FROM `user` WHERE `uid` = p_applicant_uid;
        IF v_user_count = 0 THEN
            SET p_request_id = 0;
            SET p_error_code = -1; # -1 申请人不存在
        ELSE
            SELECT COUNT(*) INTO v_user_count FROM `user` WHERE `uid` = p_receiver_uid;
            IF v_user_count = 0 THEN
                SET p_request_id = 0;
                SET p_error_code = -2; # -2 接收人不存在
            ELSE
                # 3. 检查是否已经是好友
                SELECT COUNT(*)
                INTO v_is_friend
                FROM `user_friend`
                WHERE `uid` = p_applicant_uid
                  AND `friend_uid` = p_receiver_uid;

                IF v_is_friend > 0 THEN
                    SET p_request_id = 0;
                    SET p_error_code = -4; # -4 已经是好友
                ELSE
                    # 4. 检查对方是否已经申请过自己
                    SELECT COUNT(*)
                    INTO v_is_reverse
                    FROM `friend_request`
                    WHERE `applicant_uid` = p_receiver_uid
                      AND `receiver_uid` = p_applicant_uid
                      AND `status` = 0;

                    IF v_is_reverse > 0 THEN
                        SET p_request_id = 0;
                        SET p_error_code = -5; # -5 对方已申请你，请直接同意
                    ELSE
                        # 5. 检查是否已存在申请记录（历史被拒绝的记录）
                        SELECT COUNT(*)
                        INTO v_exist_req
                        FROM `friend_request`
                        WHERE `applicant_uid` = p_applicant_uid
                          AND `receiver_uid` = p_receiver_uid;

                        IF v_exist_req > 0 THEN
                            # 之前申请过（可能被拒绝），更新记录重新发起，并更新备注
                            UPDATE `friend_request`
                            SET `message`    = p_message,
                                `remark`     = p_remark,
                                `status`     = 0,
                                `updated_at` = CURRENT_TIMESTAMP
                            WHERE `applicant_uid` = p_applicant_uid
                              AND `receiver_uid` = p_receiver_uid;

                            # 获取更新后的请求编号
                            SELECT `id`
                            INTO p_request_id
                            FROM `friend_request`
                            WHERE `applicant_uid` = p_applicant_uid
                              AND `receiver_uid` = p_receiver_uid
                            LIMIT 1;

                            SET p_error_code = 0;
                        ELSE
                            # 首次申请，插入新记录（包含备注）
                            INSERT INTO `friend_request` (`applicant_uid`, `receiver_uid`, `message`, `remark`)
                            VALUES (p_applicant_uid, p_receiver_uid, p_message, p_remark);

                            # 获取新插入的请求编号
                            SET p_request_id = LAST_INSERT_ID();
                            SET p_error_code = 0;
                        END IF;
                    END IF;
                END IF;
            END IF;
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `update_friend_remark` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `update_friend_remark`(IN p_uid bigint unsigned,
                                                                  IN p_friend_uid bigint unsigned,
                                                                  IN p_remark varchar(256), OUT p_result int)
BEGIN
    DECLARE v_is_friend INT DEFAULT 0;

    -- 异常捕获（防止因为备注超长或死锁导致崩溃）
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_result = -10; -- -10 代表数据库异常
        END;

    # 1. 校验是否给自己设置备注
    IF p_uid = p_friend_uid THEN
        SET p_result = -2; # -2 不能给自己设置备注

    ELSE
        # 2. 检查双方是否已经是好友关系（必须存在单向记录）
        SELECT COUNT(*)
        INTO v_is_friend
        FROM `user_friend`
        WHERE `uid` = p_uid
          AND `friend_uid` = p_friend_uid;

        IF v_is_friend = 0 THEN
            SET p_result = -1; # -1 对方不是你的好友，无法设置备注

        ELSE
            # 3. 执行单向更新
            UPDATE `user_friend`
            SET `remark` = p_remark
            WHERE `uid` = p_uid
              AND `friend_uid` = p_friend_uid;

            SET p_result = 0; # 0 代表设置成功
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `update_group_info` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`%` PROCEDURE `update_group_info`(
    IN p_group_id BIGINT UNSIGNED,
    IN p_operator_id BIGINT UNSIGNED, -- 操作者，用于权限校验
    IN p_name VARCHAR(255) CHARACTER SET utf8mb4, -- NULL 保留原值
    IN p_avatar_url VARCHAR(255) CHARACTER SET utf8mb4, -- NULL 保留原值，'' 表示清空
    IN p_announcement TEXT CHARACTER SET utf8mb4, -- NULL 保留原值，'' 表示清空
    IN p_max_members INT UNSIGNED, -- NULL 保留原值
    OUT p_error INT
)
    MODIFIES SQL DATA
    SQL SECURITY INVOKER
    COMMENT '修改群聊信息，NULL 参数保留原值'
BEGIN
    DECLARE v_member_count INT UNSIGNED DEFAULT NULL;
    DECLARE v_role VARCHAR(16) DEFAULT NULL;

    -- 任何未捕获的 SQL 异常统一归为数据库错误
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_error = -10;
        END;

    SET p_error = 0;

    -- 1. 群是否存在（group_id 为主键，查不到时 v_member_count 保持 NULL）
    SELECT `member_count`
    INTO v_member_count
    FROM `groups`
    WHERE `id` = p_group_id;

    IF v_member_count IS NULL THEN
        SET p_error = -1; -- 群不存在
    ELSE
        -- 2. 只有群主 / 管理员可以修改
        SELECT `role`
        INTO v_role
        FROM `group_members`
        WHERE `group_id` = p_group_id
          AND `user_id` = p_operator_id;

        IF v_role IS NULL OR v_role NOT IN ('owner', 'admin') THEN
            SET p_error = -2; -- 权限不足
        -- 3. 参数校验（NULL 已在上面排除，这里只校验真正提交的值）
        ELSEIF p_name IS NOT NULL AND TRIM(p_name) = '' THEN
            SET p_error = -3; -- 群名不能为空串
        ELSEIF p_max_members IS NOT NULL
            AND (p_max_members = 0 OR p_max_members < v_member_count) THEN
            SET p_error = -3; -- 不能小于当前成员数
        ELSE
            -- 4. 核心：COALESCE(NULL, 原值) = 原值；非 NULL（含空串）才覆盖
            UPDATE `groups`
            SET `name`         = COALESCE(p_name, `name`),
                `avatar_url`   = COALESCE(p_avatar_url, `avatar_url`),
                `announcement` = COALESCE(p_announcement, `announcement`),
                `max_members`  = COALESCE(p_max_members, `max_members`)
            WHERE `id` = p_group_id;

            SET p_error = 0;
        END IF;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!50003 DROP PROCEDURE IF EXISTS `update_user_info` */;
/*!50003 SET @saved_cs_client = @@character_set_client */;
/*!50003 SET @saved_cs_results = @@character_set_results */;
/*!50003 SET @saved_col_connection = @@collation_connection */;
/*!50003 SET character_set_client = utf8mb4 */;
/*!50003 SET character_set_results = utf8mb4 */;
/*!50003 SET collation_connection = utf8mb4_0900_ai_ci */;
/*!50003 SET @saved_sql_mode = @@sql_mode */;
/*!50003 SET sql_mode =
        'ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION' */;
DELIMITER ;;
CREATE
    DEFINER = `root`@`localhost` PROCEDURE `update_user_info`(IN p_uid bigint unsigned, IN p_name varchar(64),
                                                              IN p_bio varchar(256), IN p_icon varchar(512),
                                                              IN p_sex tinyint, OUT p_error_code int)
BEGIN
    DECLARE v_user_count INT DEFAULT 0;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
        BEGIN
            SET p_error_code = -10;
        END;

    SET p_error_code = 0;


    SELECT COUNT(*) INTO v_user_count FROM `user` WHERE `uid` = p_uid;

    IF v_user_count = 0 THEN
        SET p_error_code = -1;
    ELSE
        UPDATE `user`
        SET `name` = IFNULL(NULLIF(p_name, ''), `name`),
            `bio`  = IFNULL(p_bio, `bio`),
            `icon` = IFNULL(p_icon, `icon`),
            `sex`  = IFNULL(p_sex, `sex`)
        WHERE `uid` = p_uid;

        SET p_error_code = 0;
    END IF;
END ;;
DELIMITER ;
/*!50003 SET sql_mode = @saved_sql_mode */;
/*!50003 SET character_set_client = @saved_cs_client */;
/*!50003 SET character_set_results = @saved_cs_results */;
/*!50003 SET collation_connection = @saved_col_connection */;
/*!40103 SET TIME_ZONE = @OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE = @OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS = @OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS = @OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT = @OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS = @OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION = @OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES = @OLD_SQL_NOTES */;

