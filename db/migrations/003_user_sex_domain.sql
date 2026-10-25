-- 003 user.sex 取值域对齐代码：0男 / 1女 / 2未知
--
-- 问题：三处真相互相矛盾（已核对）——
--   src/public/data/UserInfo.h:8        enum Sex : unsigned char { MALE, FEMALE, UNKNOWN }  -> 0/1/2
--   qtClient/qml/dialog/UserInfoDialog.qml:705  sexValue() 0男 / 1女 / 其它 -> 未知
--   docs/api-changes.md:291、docs/api-friend.md:224     0男 / 1女 / 2未知
--   但数据库：`sex` tinyint DEFAULT 3，注释写「1男/2女/3未知」。
--   => reg_user 不写 sex，新用户一律落库 3；枚举里根本没有 3，客户端靠 default 分支
--      才显示成「未知」（碰巧对），而 uid 1001 的 sex=1 在代码语义下是「女」。
--
-- 本次：把域外值归一为 2（语义同为「未知」，不改变任何显示），默认值改 2，并加 CHECK。
-- 注意：uid 1001 的 sex=1 按代码语义就是「女」。如果本意是「男」，请单独执行
--       UPDATE user SET sex = 0 WHERE uid = 1001;  —— 本迁移不动它。
--
-- 回滚：ALTER TABLE `user` DROP CHECK `ck_user_sex`,
--       MODIFY COLUMN `sex` tinyint NOT NULL DEFAULT 3 COMMENT '1男/2女/3未知';

UPDATE `user`
SET `sex` = 2
WHERE `sex` NOT IN (0, 1, 2);

ALTER TABLE `user`
    MODIFY COLUMN `sex` tinyint NOT NULL DEFAULT 2 COMMENT '0男/1女/2未知（对齐 enum Sex）',
    ADD CONSTRAINT `ck_user_sex` CHECK (`sex` IN (0, 1, 2));

INSERT INTO `schema_migrations` (`version`)
VALUES ('003_user_sex_domain') ON DUPLICATE KEY
UPDATE `version` = `version`;
