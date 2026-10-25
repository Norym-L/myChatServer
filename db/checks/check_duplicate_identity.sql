-- 身份唯一性体检：重名 / 同邮箱多账号。建 UNIQUE 之前必须先清空本结果。
SELECT 'dup_name' AS kind, `name` AS value, COUNT(*) AS n, GROUP_CONCAT(`uid` ORDER BY `uid`) AS uids
FROM `user`
GROUP BY `name`
HAVING n > 1
UNION ALL
SELECT 'dup_email', `email`, COUNT(*), GROUP_CONCAT(`uid` ORDER BY `uid`)
FROM `user`
GROUP BY `email`
HAVING COUNT(*) > 1
ORDER BY kind, n DESC;
