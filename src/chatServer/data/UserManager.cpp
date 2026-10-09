#include "UserManager.h"

#include <utility>

UserManager::~UserManager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uidToSession.clear();
}

std::shared_ptr<CSession> UserManager::getSession(const std::uint64_t uid)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (const auto it = m_uidToSession.find(uid); it != m_uidToSession.end())
    {
        return it->second;
    }
    else
    {
        return nullptr;
    }
}

void UserManager::setUserSession(const std::uint64_t uid, std::shared_ptr<CSession> session)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uidToSession[uid] = std::move(session);
}

void UserManager::removeUserSession(const std::uint64_t uid)
{
    const auto uid_str = std::to_string(uid);
    // 可能在本服务器找不到key
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_uidToSession.erase(uid);
    }
}
