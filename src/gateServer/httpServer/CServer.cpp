#include "CServer.h"
#include "AsioIOServicePool.h"
#include "HttpConnection.h"
#include <spdlog/spdlog.h>

CServer::CServer(net::io_context &ioc, const unsigned short &port)
    : m_acceptor(ioc, tcp::endpoint(tcp::v4(), port)),
      m_ioc(ioc)
{}

void CServer::start()
{
    auto self = shared_from_this();
    auto &io_context = AsioIOServicePool::GetInstance().getIOServer();

    const auto new_connection = std::make_shared<HttpConnection>(io_context);
    m_acceptor.async_accept(new_connection->getSocket(), [self, new_connection](const beast::error_code &ec)
    {
        try
        {
            // 出错放弃此链接，继续监听其他链接
            if (ec)
            {
                spdlog::warn("[CServer] CServer link error: {} , create a new link", ec.what());
                self->start();
                return;
            }

            // 启动链接
            new_connection->start();

            self->start();
        }
        catch (std::exception &exp)
        {
            spdlog::error("[CServer] CServer error: {}", exp.what());
        }
    });
}
