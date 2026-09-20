#include "CServer.h"

#include <spdlog/spdlog.h>

#include "AsioIOServicePool.h"

namespace chat
{
CServer::CServer(boost::asio::io_context& io_context, const short port)
    : m_ioContext(io_context), m_port(port), m_acceptor(io_context, tcp::endpoint(tcp::v4(), port))
{
    spdlog::info("[chat server] start success, listen on port: {}", m_port);
    startAccept();
}

CServer::~CServer()
{
    spdlog::info("[chat server] desturct listen on port: {}", m_port);
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
        new_session->start();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessions.insert(std::make_pair(new_session->getUuid(), new_session));
    }
    else
    {
        spdlog::error("[server] session accept failed, error: {}", error_code.what());
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
}
