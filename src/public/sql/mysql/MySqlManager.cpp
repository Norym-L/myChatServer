#include "MySqlManager.h"

int MySqlManager::registerUser(const std::string& email, const std::string& name, const std::string& password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

std::uint64_t MySqlManager::sendFriendRequest(const std::uint64_t applicant_uid, const std::uint64_t receiver_uid, const std::string& verify_message, const std::string& remark) const
{
    return m_mySqlDao.sendFriendRequest(applicant_uid, receiver_uid, verify_message, remark);
}

int MySqlManager::handleFriendRequest(const std::uint64_t request_id, const std::uint64_t receiver_uid, const FriendApplyStatus status) const
{
    return m_mySqlDao.handleFriendRequest(request_id, receiver_uid, status);
}

int MySqlManager::deleteFriend(const std::uint64_t uid, const std::uint64_t friend_uid) const
{
    return m_mySqlDao.deleteFriend(uid, friend_uid);
}

int MySqlManager::updateFriendRemark(const std::uint64_t uid, const std::uint64_t friend_uid, const std::string& remark) const
{
    return m_mySqlDao.updateFriendRemark(uid, friend_uid, remark);
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

std::vector<std::shared_ptr<ApplyInfo>> MySqlManager::getFriendRequestListByUid(const uint64_t uid)
{
    return m_mySqlDao.getFriendRequestListByUid(uid);
}

MySqlManager::MySqlManager() = default;
