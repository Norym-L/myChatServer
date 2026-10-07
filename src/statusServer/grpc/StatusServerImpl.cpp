#include "StatusServerImpl.h"
#include <const.h>
#include <string_view>
#include <boost/uuid.hpp>
#include <ConfigManager.h>
#include <RedisManager.h>


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
    const auto server_list = config["ChatServer"]["servers"];

    std::vector<std::string> chat_servers{};
    {
        std::stringstream stringstream(server_list);
        std::string chat_server;

        while (std::getline(stringstream, chat_server, ','))
        {
            chat_servers.push_back(chat_server);
        }
    }

    for (const auto& chat_server: chat_servers)
    {
        ChatServer server{
            .host             = config[chat_server]["host"],
            .port             = config[chat_server]["port"],
            .name             = config[chat_server]["name"],
            .connection_count = 0
        };
        m_servers.emplace(server.name, server);
    }
}

ChatServer StatusServerImpl::getChatServer()
{
    std::lock_guard<std::mutex> lock(m_serverMutex);
    ChatServer min_server{.connection_count = ~0ull};
    for (auto& server: m_servers | std::views::values)
    {
        if (server.name == min_server.name)
        {
            continue;
        }

        const auto count_str = RedisManager::GetInstance().hGet(LOGIN_COUNT, server.name);
        if (count_str.empty())
        {
            server.connection_count = ~0ull;
        }
        else
        {
            server.connection_count = std::stoull(count_str);
        }

        if (server.connection_count < min_server.connection_count)
        {
            min_server = server;
        }
    }

    if (min_server.connection_count == ~0ull)
    {
        spdlog::error("no avaible chat server to return");
        throw std::runtime_error("no avaible chat server to return");
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
    InsertToken(request->uid(), token);
    return Status::OK;
}

Status StatusServerImpl::Login(grpc::ServerContext* context, const message::LoginReq* request, message::LoginRsp* response)
{
    const auto uid = request->uid();
    auto token = request->token();

    std::string token_value{};
    if (!RedisManager::GetInstance().get(USER_TOKEN_PERFIX + std::to_string(uid), token_value))
    {
        response->set_error(ErrorCode::USER_UID_INVALID);
        return Status::OK;
    }
    if (token_value != token)
    {
        response->set_error(ErrorCode::USER_TOKEN_INVALID);
        return Status::OK;
    }

    response->set_error(ErrorCode::SUCCESS);
    response->set_uid(uid);
    response->set_token(token);
    return Status::OK;
}

void StatusServerImpl::InsertToken(const size_t uid, const std::string& token)
{
    (void)RedisManager::GetInstance().set(USER_TOKEN_PERFIX + std::to_string(uid), token);
}
