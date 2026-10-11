#include "StatusServerImpl.h"
#include <charconv>
#include <cstdint>
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

    const ChatServer *min_server = nullptr;
    for (auto& server: m_servers | std::views::values)
    {
        // 计数器缺失（全新 Redis、或该实例还没有用户登录过）视为 0：
        // 之前把它当作 ~0ull 会导致新部署时全部实例都被判为不可用而直接抛错，
        // /user_login 永远拿不到聊天服务器
        const auto count_str = RedisManager::GetInstance().hGet(LOGIN_COUNT, server.name);
        std::uint64_t count = 0;
        if (!count_str.empty())
        {
            std::from_chars(count_str.data(), count_str.data() + count_str.size(), count);
        }
        server.connection_count = count;

        if (min_server == nullptr || server.connection_count < min_server->connection_count)
        {
            min_server = &server;
        }
    }

    if (min_server == nullptr)
    {
        spdlog::error("no available chat server to return");
        throw std::runtime_error("no available chat server to return");
    }

    // 先拷贝再自增：返回值语义与原来一致（返回分发前的连接数）
    auto selected = *min_server;
    ++m_servers[selected.name].connection_count;
    return selected;
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
    const std::uint64_t uid = request->uid();
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

void StatusServerImpl::InsertToken(const std::uint64_t uid, const std::string& token)
{
    // 写入失败不能静默：token 没进 Redis 时，ChatServer 侧校验会一律返回 USER_UID_INVALID
    if (!RedisManager::GetInstance().set(USER_TOKEN_PERFIX + std::to_string(uid), token))
    {
        spdlog::error("[status server] failed to insert token into redis, uid: {}", uid);
    }
}
