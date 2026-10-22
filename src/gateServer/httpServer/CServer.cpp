#include "CServer.h"
#include <AsioIOServicePool.h>
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
        if (ec)
        {
            // 本次 accept 失败：记录后继续监听
            SPDLOG_WARN("[CServer] CServer link error: {} , create a new link", ec.what());
        }
        else
        {
            try
            {
                // 启动链接
                new_connection->start();
            }
            catch (std::exception &exp)
            {
                SPDLOG_ERROR("[CServer] CServer new connection start error: {}", exp.what());
            }
        }

        // 无论成功与否都重新挂起 accept。放在异常处理之外，保证：
        //   1) 异常不会让监听循环中断；
        //   2) 每个 accept 完成事件只重新挂起一次，不会重复 accept
        try
        {
            self->start();
        }
        catch (std::exception &exp)
        {
            SPDLOG_ERROR("[CServer] CServer re-accept error: {}", exp.what());
        }
    });
}
