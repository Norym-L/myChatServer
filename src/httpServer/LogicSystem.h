/**
 * @file LogicSystem.h
 * @brief LogicSystem
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_LOGICSYSTEM_H
#define GATESERVER_LOGICSYSTEM_H

#include "../const.h"

class HttpConnection;
using HttpHandler = std::function<void(std::shared_ptr<HttpConnection>)>;

class LogicSystem : public TSingLeton<LogicSystem>
{
    friend class TSingLeton<LogicSystem>;

public:
    ~LogicSystem();
    bool handleGet(std::string path, std::shared_ptr<HttpConnection> connection);
    bool handlePost(std::string path, std::shared_ptr<HttpConnection> connection);
    void regGet(std::string url, HttpHandler handler);
    void regPost(std::string url, HttpHandler handler);

private:
    LogicSystem();
    std::map<std::string, HttpHandler> m_post_handlers;
    std::map<std::string, HttpHandler> m_get_handlers;
};


#endif //GATESERVER_LOGICSYSTEM_H
