#include "LogicSystem.h"
#include "HttpConnection.h"

LogicSystem::~LogicSystem() {}

bool LogicSystem::handleGet(std::string path, std::shared_ptr<HttpConnection> connection)
{
    if (!m_get_handlers.contains(path))
    {
        return false;
    }

    m_get_handlers[path](connection);
    return true;
}

void LogicSystem::regGet(std::string url, HttpHandler handler)
{
    m_get_handlers.insert(std::make_pair(url, handler));
}

LogicSystem::LogicSystem()
{
    regGet("/get_test", [](const std::shared_ptr<HttpConnection> &connection)
    {
       beast::ostream(connection->m_response.body()) << "receive get_test req";
    });
}
