#include "HttpConnection.h"

#include "LogicSystem.h"
#include <exception>
#include <spdlog/spdlog.h>

#include <utils/UrlCodec.h>

#include <boost/asio/co_spawn.hpp>

namespace
{
std::string exception_to_string(const std::exception_ptr& ep)
{
    if (!ep)
    {
        return "no exception";
    }
    try
    {
        std::rethrow_exception(ep);
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
    catch (...)
    {
        return "unknown exception";
    }
}
}

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
                // 本端因超时主动关闭时，挂起的读会以 operation_aborted 结束，属预期行为，降级为 debug
                if (self->m_beClosed)
                {
                    spdlog::debug("[HttpConnection] read aborted by local close: {}", ec.what());
                }
                else
                {
                    spdlog::error("[HttpConnection] http read err: {}", ec.what());
                }
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
    // 计时起点必须在"真正开始服务"时才确定：连接对象是在上一次 accept 完成时就被预先创建的
    // （见 CServer::start），若沿用构造时刻作为到期时刻，服务端静默超过 60 秒后新连进来的
    // 客户端会立刻被判超时并关闭 socket，导致首个请求被丢弃（客户端表现为连接被重置/无响应）
    m_deadline.expires_after(std::chrono::seconds(60));
    m_deadline.async_wait([self](const beast::error_code &ec)
    {
        if (!ec)
        {
            // 明确记录超时关闭，避免与真正的读写错误混淆
            spdlog::warn("[HttpConnection] connection timeout, close socket");
            self->m_beClosed = true;
            boost::system::error_code ignore;
            self->m_socket.shutdown(tcp::socket::shutdown_both, ignore);
            self->m_socket.close(ignore);
        }
    });
}

void HttpConnection::writeResponse()
{
    auto self = shared_from_this();
    m_response.content_length(m_response.body().size());
    http::async_write(m_socket, m_response, [self](beast::error_code ec, std::size_t bytes_transferred)
    {
        // 写失败必须记录：此前忽略该错误会让服务端日志显示"业务成功"，
        // 而客户端实际收不到响应（正是"两次都显示登录成功但第一次无效"的原因）
        if (ec)
        {
            spdlog::warn("[HttpConnection] response write failed: {}", ec.what());
        }
        boost::system::error_code ignore;
        self->m_socket.shutdown(tcp::socket::shutdown_send, ignore);
        self->m_deadline.cancel();
    });
}

void HttpConnection::handleReq()
{
    // 请求已完整到达：重新计时，使"读取阶段"与"处理 + 回写阶段"各有独立的超时窗口，
    // 避免处理（数据库/RPC/Redis）耗时较长时被读取阶段的剩余时间误杀
    checkDeadLine();
    // 设置版本
    m_response.version(m_request.version());
    m_response.keep_alive(false);

    // 处理器是协程：在这里拉起，完成后（或异常）再回写响应，全程沿用本条连接的执行器
    auto self = shared_from_this();
    net::co_spawn(m_socket.get_executor(), handleRequest(self),
                  [self](const std::exception_ptr& ep)
                      {
                          if (!ep)
                          {
                              return;
                          }
                          spdlog::error("[HttpConnection] handle request failed: {}", exception_to_string(ep));

                          // 处理器异常时返回 500，而不是让客户端一直等到超时
                          http::response<http::dynamic_body> rsp;
                          rsp.result(http::status::internal_server_error);
                          rsp.version(self->m_request.version());
                          rsp.keep_alive(false);
                          rsp.set(http::field::server, "GateServer");
                          rsp.set(http::field::content_type, "text/plain");
                          beast::ostream(rsp.body()) << "internal server error\n";
                          self->m_response = std::move(rsp);
                          self->writeResponse();
                      });
}

net::awaitable<void> HttpConnection::handleRequest(std::shared_ptr<HttpConnection> self)
{
    // get 请求
    if (m_request.method() == http::verb::get)
    {
        preParseGetParam();
        const bool success = co_await LogicSystem::GetInstance().handleGet(m_getUrl, self);
        if (!success)
        {
            m_response.result(http::status::not_found);
            m_response.set(http::field::content_type, "text/plain");
            beast::ostream(m_response.body()) << "url not found\n";
            writeResponse();
            co_return;
        }
        m_response.result(http::status::ok);
        m_response.set(http::field::server, "GateServer");
        writeResponse();
        co_return;
    }

    if (m_request.method() == http::verb::post)
    {
        const bool success = co_await LogicSystem::GetInstance().handlePost(m_request.target(), self);
        if (!success)
        {
            m_response.result(http::status::not_found);
            m_response.set(http::field::content_type, "text/plain");
            beast::ostream(m_response.body()) << "url not found\n";
            writeResponse();
            co_return;
        }
        m_response.result(http::status::ok);
        m_response.set(http::field::server, "GateServer");
        writeResponse();
        co_return;
    }

    // 非 GET/POST 方法不路由任何处理器，需显式回应 405，否则客户端只能等超时
    m_response.result(http::status::method_not_allowed);
    // HEAD 响应不应携带实体主体
    if (m_request.method() != http::verb::head)
    {
        m_response.set(http::field::content_type, "text/plain");
        beast::ostream(m_response.body()) << "method not allowed\n";
    }
    writeResponse();
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
        const size_t eq_pos = pair.find('=');
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
        const size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos)
        {
            key = url_codec::UrlDecode(query_string.substr(0, eq_pos));
            value = url_codec::UrlDecode(query_string.substr(eq_pos + 1));
            m_getParams[key] = value;
        }
    }
}
