#ifndef GATESERVER_USERMANAGER_H
#define GATESERVER_USERMANAGER_H

#include <mutex>
#include <unordered_map>

#include "Singleton.h"

class CSession;

class UserManager : public TSingLeton<UserManager>
{
    friend class TSingLeton;

public:
    ~UserManager();

    [[nodiscard]] std::shared_ptr<CSession> getSession(std::uint64_t uid);
    void setUserSession(std::uint64_t uid, std::shared_ptr<CSession> session);
    void removeUserSession(std::uint64_t uid);

private:
    UserManager() = default;

    std::mutex m_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<CSession>> m_uidToSession;
};


#endif //GATESERVER_USERMANAGER_H
