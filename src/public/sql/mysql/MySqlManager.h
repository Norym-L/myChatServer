#ifndef GATESERVER_MYSQLMANAGER_H
#define GATESERVER_MYSQLMANAGER_H

#include "MySqlDao.h"
#include <ConfigManager.h>


class MySqlManager : public TSingLeton<MySqlManager>
{
    friend class TSingLeton;

public:
    /**
     * 注册用户
     * @param email 邮箱
     * @param name 用户名
     * @param password 密码
     * @return 成功返回用户uid  -1 一个邮箱创建不能多于5个账号 -10 代表数据库异常
     */
    [[nodiscard]] int registerUser(const std::string& email, const std::string& name, const std::string& password) const;
    /**
     * 发送好友请求
     * @param applicant_uid 发送请求用户id
     * @param receiver_uid 接收请求用户id
     * @param verify_message 验证信息
     * @param remark 用户给对方的备注
     * @return 0 申请成功 -1 申请人不存在 -2 接收人不存在 -3 不能添加自己 -4 已经是好友 -5 对方已申请你，请直接同意 -10 代表数据库异常
     */
    [[nodiscard]] int sendFriendRequest(std::uint64_t applicant_uid, std::uint64_t receiver_uid, const std::string& verify_message, const std::string& remark) const;
    /**
     * 处理好友请求
     * @param request_id 好友请求编号
     * @param receiver_uid 接收请求用户id
     * @param status 验证状态
     * @return 0 处理成功 -1 申请不存在或无权处理 -2 该申请已被处理过 -10 代表数据库异常
     */
    [[nodiscard]] int handleFriendRequest(std::uint64_t request_id, std::uint64_t receiver_uid, FriendApplyStatus status) const;
    /**
     * 删除好友
     * @param uid 删除者的uid
     * @param friend_uid 被删除者的uid
     * @return 0 成功 -1 代表好友关系不存在 -10 代表数据库异常
     */
    [[nodiscard]] int deleteFriend(std::uint64_t uid, std::uint64_t friend_uid) const;
    /**
     * 修改好友备注
     * @param uid 自己uid
     * @param friend_uid 好友uid
     * @param remark 备注
     * @return 0 代表设置成功 -1 对方不是你的好友，无法设置备注 -2 不能给自己设置备注 -10 代表数据库异常
     */
    [[nodiscard]] int updateFriendRemark(std::uint64_t uid, std::uint64_t friend_uid, const std::string& remark) const;
    [[nodiscard]] bool updatePassword(std::uint64_t account, const std::string& password) const;

    /// 校验 uid 与 email 是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsAccountMatchEmail(std::uint64_t account, const std::string& email) const;
    /// 校验密码与账号是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsPasswordMatchAccount(std::uint64_t account, const std::string& password) const;

    [[nodiscard]] std::shared_ptr<UserInfo> getUser(std::uint64_t uid) const;
    [[nodiscard]] std::vector<std::shared_ptr<UserInfo> > getUser(const std::string& name) const;

private:
    MySqlManager();
    MySqlDao m_mySqlDao;
};

#endif //GATESERVER_MYSQLMANAGER_H
