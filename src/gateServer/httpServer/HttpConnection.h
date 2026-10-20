/**
 * @file HttpConnection.h
 * @brief HttpConnection
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_HTTPCONNECTION_H
#define GATESERVER_HTTPCONNECTION_H

#include <memory>

#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <string>

namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    friend class LogicSystem;

public:
    explicit HttpConnection(boost::asio::io_context &ioc);
    void start();
    tcp::socket &getSocket();

private:
    void checkDeadLine();
    void writeResponse();
    void handleReq();
    /// 请求处理协程：路由 + 处理器（内部 co_await 访问 Redis）都在本条连接的执行器上串行执行
    boost::asio::awaitable<void> handleRequest(std::shared_ptr<HttpConnection> self);
    /// 解析get请求参数
    void preParseGetParam();
    tcp::socket m_socket;
    beast::flat_buffer m_buffer{8192};
    http::request<http::dynamic_body> m_request;
    http::response<http::dynamic_body> m_response;
    net::steady_timer m_deadline{m_socket.get_executor(), std::chrono::seconds(60)};
    std::string m_getUrl;
    std::unordered_map<std::string, std::string> m_getParams;
    /// 是否由本地主动关闭（超时回收），用于把"预期中止"与真正的读错误区分开
    bool m_beClosed = false;
};


#endif //GATESERVER_HTTPCONNECTION_H
