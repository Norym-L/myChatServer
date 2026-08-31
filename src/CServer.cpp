#include "CServer.h"

#include "HttpConnection.h"

CServer::CServer(net::io_context &ioc, const unsigned short &port)
    : m_acceptor(ioc, tcp::endpoint(tcp::v4(), port))
    , m_ioc(ioc)
    , m_socket(ioc)
{}

void CServer::start()
{
    auto self = shared_from_this();
    m_acceptor.async_accept(m_socket, [self](const beast::error_code &ec)
    {
        try
        {
            // 出错放弃此链接，继续监听其他链接
            if (ec)
            {
                self->start();
                return;
            }

            // 创建新连接，使用HttpConnection管理链接
            std::make_shared<HttpConnection>(std::move(self->m_socket))->start();

            self->start();
        }
        catch (std::exception& exp)
        {
            std::cout << exp.what() << std::endl;
        }
    });
}
