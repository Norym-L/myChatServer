#ifndef GATESERVER_MYSQLMANAGER_H
#define GATESERVER_MYSQLMANAGER_H

#include "MySqlDao.h"
#include "../../Singleton.h"

class MySqlManager : public TSingLeton<MySqlManager>
{
    friend class TSingLeton;

public:
    int registerUser(const std::string &email, const std::string &name, const std::string &password) const;

    [[nodiscard]] bool checkIsAccountMatchEmail(int account, const std::string& email) const;
    [[nodiscard]] bool updatePassword(int account, const std::string &password) const;

private:
    MySqlManager();
    MySqlDao m_mySqlDao;
};

#endif //GATESERVER_MYSQLMANAGER_H
