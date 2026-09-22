#include "HttpConnection.h"

#include "LogicSystem.h"
#include <spdlog/spdlog.h>

#include <utils/UrlCodec.h>

HttpConnection::HttpConnection(boost::asio::io_context &ioc)
    : m_socket(ioc)
{}

void HttpConnection::start()
{
    auto self = shared_from_this();
    // 超时定时器必须在读取请求前启动，否则慢连接可在请求阶段挂满整个超时周期
    self->checkDeadLine();
    http::async_read(m_socket, m_buffer, m_request, [self](const beast::error_code &ec, std::size_t bytes_transferred)
    {
        try
        {
            if (ec)
            {
                spdlog::error("[HttpConnection] http read err: {}", ec.what());
                return;
            }
            boost::ignore_unused(bytes_transferred);
            self->handleReq();
        }
        catch (std::exception &exp)
        {
            spdlog::error("[HttpConnection] HttpConnection error: {}", exp.what());
        }
    });
}

tcp::socket &HttpConnection::getSocket()
{
    return m_socket;
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
        preParseGetParam();
        const bool success = LogicSystem::GetInstance().handleGet(m_getUrl, shared_from_this());
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
    }

    if (m_request.method() == http::verb::post)
    {
        const bool success = LogicSystem::GetInstance().handlePost(m_request.target(), shared_from_this());
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
    }

    // 非 GET/POST 方法不路由任何处理器，需显式回应 405，否则客户端只能等超时
    if (m_request.method() != http::verb::get &&
        m_request.method() != http::verb::post)
    {
        m_response.result(http::status::method_not_allowed);
        // HEAD 响应不应携带实体主体
        if (m_request.method() != http::verb::head)
        {
            m_response.set(http::field::content_type, "text/plain");
            beast::ostream(m_response.body()) << "method not allowed\n";
        }
        writeResponse();
    }
}

void HttpConnection::preParseGetParam()
{
    // 提取 URL
    const auto url = m_request.target();
    // 查找查询字符串的开始位置（即 '?' 的位置）
    const auto query_pos = url.find('?');
    if (query_pos == std::string::npos)
    {
        m_getUrl = url;
        return;
    }

    m_getUrl = url.substr(0, query_pos);
    std::string query_string = url.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos)
    {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos)
        {
            key = url_codec::UrlDecode(pair.substr(0, eq_pos)); // 查询参数需解码后再使用
            value = url_codec::UrlDecode(pair.substr(eq_pos + 1));
            m_getParams[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数对（如果没有 & 分隔符）
    if (!query_string.empty())
    {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos)
        {
            key = url_codec::UrlDecode(query_string.substr(0, eq_pos));
            value = url_codec::UrlDecode(query_string.substr(eq_pos + 1));
            m_getParams[key] = value;
        }
    }
}
