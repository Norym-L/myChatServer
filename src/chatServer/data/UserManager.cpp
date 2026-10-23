#include "UserManager.h"

#include <utility>

#include "../net/CSession.h"

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

void UserManager::removeUserSession(const std::uint64_t uid, const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_uidToSession.find(uid);
    if (it == m_uidToSession.end())
    {
        return;
    }
    // uid 可能已经换了新会话（旧连接晚于新登录才被服务端发现断开）：
    // 这时只允许断开的那条会话清理自己，不能把新会话的映射删掉。
    if (it->second && it->second->getSessionId() != session_id)
    {
        return;
    }
    m_uidToSession.erase(it);
}
