#include "CServer.h"

#include <spdlog/spdlog.h>

#include <utility>

#include <AsioIOServicePool.h>

CServer::CServer(boost::asio::io_context& io_context, const std::uint16_t port, std::string  server_name)
    : m_ioContext(io_context), m_port(port), m_acceptor(io_context, tcp::endpoint(tcp::v4(), port)), m_name(std::move(server_name))
{
    spdlog::info("[{}] start success, listen on port: {}", getSelfServerName(), m_port);
    startAccept();
}

CServer::~CServer()
{
    spdlog::info("[{}] destroy listen on port: {}", getSelfServerName(), m_port);
}

void CServer::clearSession(const std::string& uuid)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(uuid);
}

void CServer::handleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error_code)
{
    if (!error_code)
    {
        // 必须先登记再启动：读回调在另一个 IOCP 线程上失败时会调用 clearSession，
        // 若此刻尚未登记，该会话将无法被清理，导致 m_sessions 残留死会话
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_sessions.insert(std::make_pair(new_session->getUuid(), new_session));
        }
        new_session->start();
    }
    else
    {
        spdlog::error("[{}] session accept failed, error: {}", getSelfServerName(), error_code.what());
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

