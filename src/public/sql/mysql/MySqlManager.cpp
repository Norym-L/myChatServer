#include "MySqlManager.h"

void MySqlManager::init(net::io_context& io_context)
{
    auto& config = ConfigManager::GetInstance();

    mysql::pool_params params;
    params.server_address.emplace_host_and_port(config["Mysql"]["host"], std::stoi(config["Mysql"]["port"]));
    params.username = "root";
    params.password = config["Mysql"]["passwd"];
    params.thread_safe = true;
    params.initial_size = 4;
    params.max_size = 10;
    params.database = "chatserver";

    m_mysqlBoost.start(io_context, std::move(params));
}

int MySqlManager::registerUser(const std::string& email, const std::string& name, const std::string& password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

int MySqlManager::sendFriendRequest(std::uint64_t& add_friend_request_id, const std::uint64_t applicant_uid, const std::uint64_t receiver_uid, const std::string& verify_message, const std::string& remark) const
{
    return m_mySqlDao.sendFriendRequest(add_friend_request_id, applicant_uid, receiver_uid, verify_message, remark);
}

int MySqlManager::handleFriendRequest(std::uint64_t& sender_uid, const std::uint64_t request_id, const std::uint64_t receiver_uid, const FriendApplyStatus status) const
{
    return m_mySqlDao.handleFriendRequest(sender_uid, request_id, receiver_uid, status);
}

int MySqlManager::deleteFriend(const std::uint64_t uid, const std::uint64_t friend_uid) const
{
    return m_mySqlDao.deleteFriend(uid, friend_uid);
}

int MySqlManager::updateFriendRemark(const std::uint64_t uid, const std::uint64_t friend_uid, const std::string& remark) const
{
    return m_mySqlDao.updateFriendRemark(uid, friend_uid, remark);
}

int MySqlManager::getNotifyUserIdInAuthFriendReq(std::uint64_t& sender_uid, const std::uint64_t add_friend_request_id) const
{
    return m_mySqlDao.getNotifyUserIdInAuthFriendReq(sender_uid, add_friend_request_id);
}

MatchResult MySqlManager::checkIsAccountMatchEmail(const std::uint64_t account, const std::string& email) const
{
    return m_mySqlDao.checkIsAccountMatchEmail(email, account);
}

bool MySqlManager::updatePassword(const std::uint64_t account, const std::string& password) const
{
    return m_mySqlDao.updatePassword(account, password);
}

MatchResult MySqlManager::checkIsPasswordMatchAccount(const std::uint64_t account, const std::string& password) const
{
    return m_mySqlDao.checkIsPasswordMatchAccount(password, account);
}

std::shared_ptr<UserInfo> MySqlManager::getUser(const std::uint64_t uid) const
{
    return m_mySqlDao.getUser(uid);
}

std::vector<std::shared_ptr<UserInfo> > MySqlManager::getUser(const std::string& name) const
{
    return m_mySqlDao.getUser(name);
}

std::vector<std::shared_ptr<ApplyInfo> > MySqlManager::getFriendRequestListByUid(const uint64_t uid) const
{
    return m_mySqlDao.getFriendRequestListByUid(uid);
}

std::vector<UidAndRemarkInUserFriend> MySqlManager::getUserFriendList(const uint64_t uid) const
{
    return m_mySqlDao.getUserFriendList(uid);
}
