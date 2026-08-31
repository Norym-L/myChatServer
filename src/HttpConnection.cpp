#include "HttpConnection.h"

#include "LogicSystem.h"

HttpConnection::HttpConnection(tcp::socket socket)
    : m_socket(std::move(socket))
{}

void HttpConnection::start()
{
    auto self = shared_from_this();
    http::async_read(m_socket, m_buffer, m_request, [self](const beast::error_code &ec, std::size_t bytes_transferred)
    {
        try
        {
            if (ec)
            {
                std::cout << "http read err: " << ec.what() << std::endl;
                return;
            }
            boost::ignore_unused(bytes_transferred);
            self->handleReq();
            self->checkDeadLine();
        }
        catch (std::exception &exp)
        {
            std::cout << exp.what() << std::endl;
        }
    });
}

void HttpConnection::checkDeadLine()
{
    auto self = shared_from_this();
    m_deadline.async_wait([self](beast::error_code ec)
    {
        if (!ec)
        {
            [[maybe_unused]] auto error =
                    self->m_socket.close(ec);
        }
    });
}

void HttpConnection::writeResponse()
{
    auto self = shared_from_this();
    m_response.content_length(m_response.body().size());
    http::async_write(m_socket, m_response, [self](beast::error_code ec, std::size_t bytes_transferred)
    {
        [[maybe_unused]] auto error =
                self->m_socket.shutdown(tcp::socket::shutdown_send, ec);
        self->m_deadline.cancel();
    });
}

void HttpConnection::handleReq()
{
    // 设置版本
    m_response.version(m_request.version());
    m_response.keep_alive(false);
    // get 请求
    if (m_request.method() == http::verb::get)
    {
        const bool success = LogicSystem::GetInstance().handleGet(m_request.target(), shared_from_this());
        if (!success)
        {
            m_response.result(http::status::not_found);
            m_response.set(http::field::content_type, "text/plain");
            beast::ostream(m_response.body()) << "url not found\n";
            writeResponse();
            return;
        }
        m_response.result(http::status::ok);
        m_response.set(http::field::server, "GateServer");
        writeResponse();
        return;
    }
}
