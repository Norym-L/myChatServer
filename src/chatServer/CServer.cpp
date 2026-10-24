#include "CServer.h"

#include <spdlog/spdlog.h>

#include <utility>

#include <AsioIOServicePool.h>

#include "data/UserManager.h"
#include "utils/ServerRegistry.h"

CServer::CServer(boost::asio::io_context& io_context, const std::uint16_t port, std::string server_name)
    : m_ioContext(io_context), m_port(port), m_acceptor(io_context, tcp::endpoint(tcp::v4(), port)), m_name(std::move(server_name))
{
    SPDLOG_INFO("[{}] start success, listen on port: {}", getSelfServerName(), m_port);
    startAccept();
}

CServer::~CServer()
{
    SPDLOG_INFO("[{}] destroy listen on port: {}", getSelfServerName(), m_port);
}

void CServer::clearSession(const std::string& session_id)
{
    std::uint64_t user_id{0};
    std::shared_ptr<CSession> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (const auto it = m_sessions.find(session_id); it != m_sessions.end())
        {
            user_id = it->second->getUserId();
            session = std::move(it->second); // 把析构推迟到锁外
            m_sessions.erase(it);
        }
    }

    session.reset();
    if (user_id > 0)
    {
        // 已登录会话断开：连接数 -1（会话已被移出 m_sessions，重复 clearSession 不会再次命中，正好抵消登录时的 +1）
        ServerRegistry::GetInstance().removeConnection();
        UserManager::GetInstance().removeUserSession(user_id, session_id);
    }
}

void CServer::handleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error_code)
{
    if (!error_code)
    {
        // 必须先登记再启动：读回调在另一个 IOCP 线程上失败时会调用 clearSession，
        // 若此刻尚未登记，该会话将无法被清理，导致 m_sessions 残留死会话
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_sessions.insert(std::make_pair(new_session->getSessionId(), new_session));
        }
        new_session->start();
    }
    else
    {
        SPDLOG_ERROR("[{}] session accept failed, error: {}", getSelfServerName(), error_code.what());
    }

    startAccept();
}

void CServer::startAccept()
{
    auto& io_context = AsioIOServicePool::GetInstance().getIOServer();
    auto new_session = std::make_shared<CSession>(io_context, this);
    m_acceptor.async_accept(new_session->getSocket(),
                            std::bind(&CServer::handleAccept, this, new_session, std::placeholders::_1));
}
