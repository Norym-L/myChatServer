-- 孤儿引用体检：所有「应该加外键」的列，是否存在指向不存在行的记录。
-- 正常应全部为 0。加外键前必须先把它清零。
SELECT 'group_members.group_id' AS fk, COUNT(*) AS orphans
FROM `group_members` c
         LEFT JOIN `groups` p ON p.`id` = c.`group_id`
WHERE p.`id` IS NULL
UNION ALL
SELECT 'group_members.user_id', COUNT(*)
FROM `group_members` c
         LEFT JOIN `user` p ON p.`uid` = c.`user_id`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'groups.owner_id', COUNT(*)
FROM `groups` c
         LEFT JOIN `user` p ON p.`uid` = c.`owner_id`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'friend_request.applicant_uid', COUNT(*)
FROM `friend_request` c
         LEFT JOIN `user` p ON p.`uid` = c.`applicant_uid`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'friend_request.receiver_uid', COUNT(*)
FROM `friend_request` c
         LEFT JOIN `user` p ON p.`uid` = c.`receiver_uid`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'message.sender_id', COUNT(*)
FROM `message` c
         LEFT JOIN `user` p ON p.`uid` = c.`sender_id`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'user_friend.uid', COUNT(*)
FROM `user_friend` c
         LEFT JOIN `user` p ON p.`uid` = c.`uid`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'user_friend.friend_uid', COUNT(*)
FROM `user_friend` c
         LEFT JOIN `user` p ON p.`uid` = c.`friend_uid`
WHERE p.`uid` IS NULL
UNION ALL
SELECT 'message_attachment.file_id', COUNT(*)
FROM `message_attachment` c
         LEFT JOIN `file_object` p ON p.`id` = c.`file_id`
WHERE p.`id` IS NULL
UNION ALL
SELECT 'upload_session.file_id', COUNT(*)
FROM `upload_session` c
         LEFT JOIN `file_object` p ON p.`id` = c.`file_id`
WHERE p.`id` IS NULL;
