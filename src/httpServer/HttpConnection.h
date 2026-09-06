/**
 * @file HttpConnection.h
 * @brief HttpConnection
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_HTTPCONNECTION_H
#define GATESERVER_HTTPCONNECTION_H

#include "../const.h"

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    friend class LogicSystem;

public:
    explicit HttpConnection(boost::asio::io_context& ioc);
    void start();
    tcp::socket& getSocket();

private:
    void checkDeadLine();
    void writeResponse();
    void handleReq();
    /// 解析get请求参数
    void preParseGetParam();
    tcp::socket m_socket;
    beast::flat_buffer m_buffer{8192};
    http::request<http::dynamic_body> m_request;
    http::response<http::dynamic_body> m_response;
    net::steady_timer m_deadline{m_socket.get_executor(), std::chrono::seconds(60)};
    std::string m_getUrl;
    std::unordered_map<std::string, std::string> m_getParams;
};


#endif //GATESERVER_HTTPCONNECTION_H
