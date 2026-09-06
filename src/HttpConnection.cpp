#include "HttpConnection.h"

#include "LogicSystem.h"

namespace
{
// 10 -> 16
unsigned char ToHex(const unsigned char x)
{
    return x > 9 ? x + 55 : x + 48;
}

// 16 -> 10
unsigned char FormHex(const unsigned char x)
{
    unsigned char y = '0';
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else
        assert(0);
    // ReSharper disable once CppLocalVariableMightNotBeInitialized
    return y;
}

std::string UrlEncode(const std::string &str)
{
    std::string str_temp;
    const auto length = str.length();
    for (auto i = 0; i < length; ++i)
    {
        if (isalnum(static_cast<unsigned char>(str[i])) || // 数字、字母、符号
            str[i] == '-' ||
            str[i] == '_' ||
            str[i] == '.' ||
            str[i] == '~')
        {
            str_temp += str[i];
        }
        else if (str[i] == ' ') // 空
        {
            str_temp += '+';
        }
        else // 特殊字符 加‘%’且高四位和第四位转16进制
        {
            str_temp += '%';
            str_temp += static_cast<char>(ToHex(static_cast<unsigned char>(str[i]) >> 4));
            str_temp += static_cast<char>(ToHex(static_cast<unsigned char>(str[i]) & 0x0F));
        }
    }
    return str_temp;
}

std::string UrlDecode(const std::string &str)
{
    std::string str_temp;
    const auto length = str.length();
    for (auto i = 0; i < length; ++i)
    {
        if (str[i] == '+')
        {
            str_temp += ' ';
        }
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            const auto high = FormHex(static_cast<unsigned char>(str[++i]));
            const auto low = FormHex(static_cast<unsigned char>(str[++i]));
            str_temp += std::to_string(high << 4 | low);
        }
        else
        {
            str_temp += str[i];
        }
    }
    return str_temp;
}
}

HttpConnection::HttpConnection(boost::asio::io_context& ioc)
    : m_socket(ioc)
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

tcp::socket & HttpConnection::getSocket()
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
}

void HttpConnection::preParseGetParam() {
    // 提取 URL
    const auto url = m_request.target();
    // 查找查询字符串的开始位置（即 '?' 的位置）
    const auto query_pos = url.find('?');
    if (query_pos == std::string::npos) {
        m_getUrl = url;
        return;
    }

    m_getUrl = url.substr(0, query_pos);
    std::string query_string = url.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos) {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(pair.substr(0, eq_pos)); // 假设有 url_decode 函数来处理URL解码
            value = UrlDecode(pair.substr(eq_pos + 1));
            m_getParams[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数对（如果没有 & 分隔符）
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));
            value = UrlDecode(query_string.substr(eq_pos + 1));
            m_getParams[key] = value;
        }
    }
}
