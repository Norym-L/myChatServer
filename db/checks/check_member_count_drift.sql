-- 冗余计数对账：groups.member_count 与 group_members 实际行数是否一致。正常应返回 0 行。
-- 注意：`stored` 是 MySQL 8 保留字，必须加反引号。
SELECT g.`id`,
       g.`name`,
       g.`member_count`                       AS `stored`,
       COUNT(gm.`user_id`)                    AS `actual`,
       g.`member_count` - COUNT(gm.`user_id`) AS `diff`
FROM `groups` g
         LEFT JOIN `group_members` gm ON gm.`group_id` = g.`id`
GROUP BY g.`id`, g.`name`, g.`member_count`
HAVING g.`member_count` <> COUNT(gm.`user_id`);
