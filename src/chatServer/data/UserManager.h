#ifndef GATESERVER_USERMANAGER_H
#define GATESERVER_USERMANAGER_H

#include <mutex>
#include <string>
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
    /// 只移除 session_id 对应的映射：旧连接晚于新登录断开时，不能把新会话的映射一起抹掉。
    void removeUserSession(std::uint64_t uid, const std::string& session_id);

private:
    UserManager() = default;

    std::mutex m_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<CSession>> m_uidToSession;
};


#endif //GATESERVER_USERMANAGER_H
