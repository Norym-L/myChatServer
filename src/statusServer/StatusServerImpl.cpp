#include "StatusServerImpl.h"
#include <const.h>
#include <boost/uuid.hpp>
#include <ConfigManager.h>


/// 生成token
static std::string generate_unique_string()
{
    const boost::uuids::uuid uuid = boost::uuids::random_generator()();
    std::string unique_string = to_string(uuid);
    return unique_string;
}

StatusServerImpl::StatusServerImpl()
{
    auto &config = ConfigManager::GetInstance();
    ChatServer server;
    server.port = config["ChatServer1"]["port"];
    server.host = config["ChatServer1"]["host"];
    m_servers.push_back(server);

    server.port = config["ChatServer2"]["port"];
    server.host = config["ChatServer2"]["host"];
    m_servers.push_back(server);
}

Status StatusServerImpl::GetChatServer(grpc::ServerContext *context, const message::GetChatServerReq *request, message::GetChatServerRsp *response)
{
    std::string prefix("status server has receive: ");
    m_server_index = (m_server_index++) % static_cast<int>(m_servers.size());
    auto &[host, port] = m_servers[m_server_index];
    spdlog::info("a new chat server has start at {}:{}", host, port);
    response->set_host(host);
    response->set_port(port);
    response->set_error(static_cast<uint32_t>(ErrorCode::SUCCESS));
    response->set_token(generate_unique_string());
    return Status::OK;
}
