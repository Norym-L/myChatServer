/**
 * @file HttpConnection.h
 * @brief HttpConnection
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_HTTPCONNECTION_H
#define GATESERVER_HTTPCONNECTION_H

#include "const.h"

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
public:
    friend class LogicSystem;
    explicit HttpConnection(tcp::socket socket);
    void start();

private:
    void checkDeadLine();
    void writeResponse();
    void handleReq();
    tcp::socket m_socket;
    beast::flat_buffer m_buffer{ 8192 };
    http::request<http::dynamic_body> m_request;
    http::response<http::dynamic_body> m_response;
    net::steady_timer m_deadline { m_socket.get_executor(), std::chrono::seconds(60) };
};


#endif //GATESERVER_HTTPCONNECTION_H
