#include "MySqlManager.h"

int MySqlManager::registerUser(const std::string &email, const std::string &name, const std::string &password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

MatchResult MySqlManager::checkIsAccountMatchEmail(const int account, const std::string &email) const
{
    return m_mySqlDao.checkIsAccountMatchEmail(email, account);
}

bool MySqlManager::updatePassword(const int account, const std::string &password) const
{
    return m_mySqlDao.updatePassword(account, password);
}

MatchResult MySqlManager::checkIsPasswordMatchAccount(const int account, const std::string &password) const
{
    return m_mySqlDao.checkIsPasswordMatchAccount(password, account);
}

std::shared_ptr<UserInfo> MySqlManager::getUser(const size_t uid) const
{
    return m_mySqlDao.getUser(uid);
}

MySqlManager::MySqlManager() = default;
