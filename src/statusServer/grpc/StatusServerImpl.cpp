#include "StatusServerImpl.h"
#include <charconv>
#include <cstdint>
#include <optional>
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

// gRPC 处理器是同步的：把 Redis 协程放到 RedisManager 自己的线程上执行并等待结果。
// 命令失败时按旧实现的语义降级（读不到当作不存在），不把异常抛给 gRPC 调用方。
namespace
{
std::optional<std::string> redisGet(const std::string& key)
{
    try
    {
        return RedisManager::GetInstance().wait(RedisManager::GetInstance().get(key));
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[status server] redis get failed, key: {}, error: {}", key, e.what());
        return std::nullopt;
    }
}

std::optional<std::string> redisHGet(const std::string& key, const std::string& field)
{
    try
    {
        return RedisManager::GetInstance().wait(RedisManager::GetInstance().hGet(key, field));
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[status server] redis hget failed, key: {}, field: {}, error: {}", key, field, e.what());
        return std::nullopt;
    }
}

bool redisExists(const std::string& key)
{
    try
    {
        return RedisManager::GetInstance().wait(RedisManager::GetInstance().exists(key));
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[status server] redis exists failed, key: {}, error: {}", key, e.what());
        return false;
    }
}

bool redisSet(const std::string& key, const std::string& value)
{
    try
    {
        RedisManager::GetInstance().wait(RedisManager::GetInstance().set(key, value));
        return true;
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[status server] redis set failed, key: {}, error: {}", key, e.what());
        return false;
    }
}
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

    // 候选集优先取「已注册且存活」的实例：
    //  - serverlist hash 存在该 name      -> 实例已向 Redis 注册（长期保存）
    //  - serverheartbeat_<name> 未过期     -> 实例当前存活（30s TTL，由实例自身续期）
    std::vector<ChatServer*> candidates;
    candidates.reserve(m_servers.size());
    for (auto& server: m_servers | std::views::values)
    {
        const auto registered_addr = redisHGet(SERVER_LIST, server.name);
        const bool registered = registered_addr && !registered_addr->empty();
        if (registered && redisExists(SERVER_HEARTBEAT_PREFIX + server.name))
        {
            candidates.push_back(&server);
        }
    }

    // 没有任何存活实例（全新部署、Redis 被清空、或实例尚未完成注册）时回落全量，
    // 否则 /user_login 会因为分发不出聊天服而整体失败
    if (candidates.empty())
    {
        SPDLOG_WARN("[status server] no alive chat server registered in redis, fallback to all configured servers");
        for (auto& server: m_servers | std::views::values)
        {
            candidates.push_back(&server);
        }
    }

    const ChatServer* min_server = nullptr;
    for (ChatServer* server: candidates)
    {
        // 计数器缺失（全新 Redis、实例重启、或该实例还没有人登录）视为 0：
        // logincount_<name> 带 30s TTL 且由实例自身续期，实例死掉后自动消失，不会残留脏计数
        const auto count_str = redisGet(LOGIN_COUNT_PREFIX + server->name);
        std::uint64_t count = 0;
        if (count_str && !count_str->empty())
        {
            std::from_chars(count_str->data(), count_str->data() + count_str->size(), count);
        }
        server->connection_count = count;

        if (min_server == nullptr || server->connection_count < min_server->connection_count)
        {
            min_server = server;
        }
    }

    if (min_server == nullptr)
    {
        SPDLOG_ERROR("no available chat server to return");
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

    SPDLOG_INFO("[status server] return a chat server that name: {}, at {}:{}", server.name, server.host, server.port);
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

    const auto token_value = redisGet(USER_TOKEN_PERFIX + std::to_string(uid));
    if (!token_value)
    {
        response->set_error(ErrorCode::USER_UID_INVALID);
        return Status::OK;
    }
    if (*token_value != token)
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
    if (!redisSet(USER_TOKEN_PERFIX + std::to_string(uid), token))
    {
        SPDLOG_ERROR("[status server] failed to insert token into redis, uid: {}", uid);
    }
}
