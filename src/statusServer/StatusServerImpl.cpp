#include "StatusServerImpl.h"
#include <const.h>
#include <string_view>
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
    auto& config = ConfigManager::GetInstance();
    ChatServer server;
    server.port = config["ChatServer1"]["port"];
    server.host = config["ChatServer1"]["host"];
    server.name = config["ChatServer1"]["name"];
    server.connection_count = 0;
    m_servers.emplace(server.name, server);

    server.port = config["ChatServer2"]["port"];
    server.host = config["ChatServer2"]["host"];
    server.name = config["ChatServer2"]["name"];
    server.connection_count = 0;
    m_servers.emplace(server.name, server);
}

ChatServer StatusServerImpl::getChatServer()
{
    std::lock_guard<std::mutex> lock(m_serverMutex);
    auto min_server = m_servers.begin()->second;
    for (const auto& server: m_servers | std::views::values)
    {
        if (server.connection_count < min_server.connection_count)
        {
            min_server = server;
        }
    }

    m_servers[min_server.name].connection_count++;
    return min_server;
}

Status StatusServerImpl::GetChatServer(grpc::ServerContext* context, const message::GetChatServerReq* request, message::GetChatServerRsp* response)
{
    constexpr std::string_view prefix("status server has received: ");
    auto const& server = getChatServer();

    spdlog::info("[status server] return a chat server that name: {}, at {}:{}", server.name, server.host, server.port);
    response->set_host(server.host);
    response->set_port(server.port);
    response->set_error(ErrorCode::SUCCESS);
    auto token = generate_unique_string();
    response->set_token(token);
    insertToken(request->uid(), token);
    return Status::OK;
}

Status StatusServerImpl::Login(grpc::ServerContext* context, const message::LoginReq* request, message::LoginRsp* response)
{
    const auto uid = request->uid();
    auto token = request->token();
    std::lock_guard<std::mutex> lock(m_tokenMutex);
    const auto iter = m_tokens.find(uid);
    if (iter == m_tokens.end())
    {
        response->set_error(ErrorCode::USER_UID_INVALID);
        return Status::OK;
    }
    if (iter->second != token)
    {
        response->set_error(ErrorCode::USER_TOKEN_INVALID);
        return Status::OK;
    }

    response->set_error(ErrorCode::SUCCESS);
    response->set_uid(uid);
    response->set_token(token);
    return Status::OK;
}

void StatusServerImpl::insertToken(size_t uid, std::string token)
{
    // m_tokens 需加锁并发保护，且必须覆盖写：emplace 对已存在 uid 会静默失败，导致二次登录 token 校验必败
    std::lock_guard<std::mutex> lock(m_tokenMutex);
    m_tokens[uid] = std::move(token);
}
