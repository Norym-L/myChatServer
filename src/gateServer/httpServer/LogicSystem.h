/**
 * @file LogicSystem.h
 * @brief LogicSystem
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_LOGICSYSTEM_H
#define GATESERVER_LOGICSYSTEM_H

#include <functional>
#include <map>
#include <memory>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "Singleton.h"

class HttpConnection;

// 处理回调是协程：内部用 co_await 访问 Redis，不阻塞所在的工作线程
using HttpHandler = std::function<boost::asio::awaitable<void>(std::shared_ptr<HttpConnection>)>;

class LogicSystem : public TSingLeton<LogicSystem>
{
    friend class TSingLeton<LogicSystem>;

public:
    ~LogicSystem();
    boost::asio::awaitable<bool> handleGet(std::string path, std::shared_ptr<HttpConnection> connection);
    boost::asio::awaitable<bool> handlePost(std::string path, std::shared_ptr<HttpConnection> connection);
    void regGet(std::string url, HttpHandler handler);
    void regPost(std::string url, HttpHandler handler);

private:
    LogicSystem();
    std::map<std::string, HttpHandler> m_post_handlers;
    std::map<std::string, HttpHandler> m_get_handlers;
};


#endif //GATESERVER_LOGICSYSTEM_H
