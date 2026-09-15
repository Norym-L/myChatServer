#include "MySqlManager.h"

int MySqlManager::registerUser(const std::string &email, const std::string &name, const std::string &password) const
{
    return m_mySqlDao.registerUser(email, name, password);
}

bool MySqlManager::updatePassword(const std::string &email, const std::string &password) const
{
    return m_mySqlDao.updatePassword(email, password);
}

MySqlManager::MySqlManager() {}
