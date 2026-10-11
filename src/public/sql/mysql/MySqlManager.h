#ifndef GATESERVER_MYSQLMANAGER_H
#define GATESERVER_MYSQLMANAGER_H

#include "MySqlDao.h"
#include <ConfigManager.h>


class MySqlManager : public TSingLeton<MySqlManager>
{
    friend class TSingLeton;

public:
    int registerUser(const std::string &email, const std::string &name, const std::string &password) const;

    [[nodiscard]] MatchResult checkIsAccountMatchEmail(std::uint64_t account, const std::string &email) const;
    [[nodiscard]] bool updatePassword(std::uint64_t account, const std::string &password) const;
    [[nodiscard]] MatchResult checkIsPasswordMatchAccount(std::uint64_t account, const std::string &password) const;
    [[nodiscard]] std::shared_ptr<UserInfo> getUser(std::uint64_t uid) const;
    [[nodiscard]] std::vector<std::shared_ptr<UserInfo>> getUser(const std::string& name) const;
private:
    MySqlManager();
    MySqlDao m_mySqlDao;
};

#endif //GATESERVER_MYSQLMANAGER_H
