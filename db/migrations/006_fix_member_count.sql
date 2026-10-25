-- 006 数据修复：groups.member_count 与 group_members 对账
--
-- 背景：member_count 是冗余计数，由存储过程手工 +1/-1 维护。实测已经漂移：
--       group id=1「自检群」 member_count=1，而 group_members 里实际有 2 行。
--       一旦计数错，add_group_member 的 `member_count < max_members` 满员判断跟着错。
--
-- 做法：以 group_members 为唯一真相重算（幂等，可重复执行）。
-- 复查：db/checks/check_member_count_drift.sql 应返回 0 行。

UPDATE `groups` g
    JOIN (SELECT `group_id`, COUNT (*) AS c FROM `group_members` GROUP BY `group_id`) m
ON m.group_id = g.id
    SET g.member_count = m.c
WHERE g.member_count <> m.c;

-- 一个成员都没有的群，计数归零
UPDATE `groups` g
    LEFT JOIN `group_members` m
ON m.group_id = g.id
    SET g.member_count = 0
WHERE m.group_id IS NULL
  AND g.member_count <> 0;

INSERT INTO `schema_migrations` (`version`)
VALUES ('006_fix_member_count') ON DUPLICATE KEY
UPDATE `version` = `version`;
