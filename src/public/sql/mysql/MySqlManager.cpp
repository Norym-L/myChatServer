#include "MySqlManager.h"

int MySqlManager::registerUser(const std::string &email, const std::string &name, const std::string &password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

MatchResult MySqlManager::checkIsAccountMatchEmail(const std::uint64_t account, const std::string &email) const
{
    return m_mySqlDao.checkIsAccountMatchEmail(email, account);
}

bool MySqlManager::updatePassword(const std::uint64_t account, const std::string &password) const
{
    return m_mySqlDao.updatePassword(account, password);
}

MatchResult MySqlManager::checkIsPasswordMatchAccount(const std::uint64_t account, const std::string &password) const
{
    return m_mySqlDao.checkIsPasswordMatchAccount(password, account);
}

std::shared_ptr<UserInfo> MySqlManager::getUser(const std::uint64_t uid) const
{
    return m_mySqlDao.getUser(uid);
}

std::vector<std::shared_ptr<UserInfo>> MySqlManager::getUser(const std::string& name) const
{
    return m_mySqlDao.getUser(name);
}

MySqlManager::MySqlManager() = default;
