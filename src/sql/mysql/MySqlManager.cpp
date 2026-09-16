#include "MySqlManager.h"

int MySqlManager::registerUser(const std::string &email, const std::string &name, const std::string &password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

bool MySqlManager::checkIsAccountMatchEmail(const int account, const std::string &email) const
{
    return m_mySqlDao.checkIsAccountMatchEmail(email, account);
}

bool MySqlManager::updatePassword(const int account, const std::string &password) const
{
    return m_mySqlDao.updatePassword(account, password);
}

bool MySqlManager::chechIsPasswordMatchAccount(const int account, const std::string &password) const
{
    return m_mySqlDao.chechIsPasswordMAtchAccount(password, account);
}

MySqlManager::MySqlManager() {}
