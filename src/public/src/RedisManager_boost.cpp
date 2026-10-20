#include <boost/redis/src.hpp>
#include "RedisManager_boost.h"

#include <json/value.h>

#include "ConfigManager.h"
#include "MessageType.h"
#include "data/UserInfo.h"

net::awaitable<bool> RedisManager::registUser(const std::uint64_t uid, std::string server_name)
{
    const auto uid_str = std::to_string(uid);
    try
    {
        co_await set(USER_IP_PERFIX + uid_str, std::move(server_name));
        co_await streamCreateGroup(USER_OFFLINE_MESSAGE_STREAM + uid_str, USER_XGROUP_NAME + uid_str);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] user: {} register failed, error: {}", uid, e.what());
        co_return false;
    }
    co_return true;
}

net::awaitable<void> RedisManager::insert(const std::vector<std::shared_ptr<UserInfo> > user_infos, const long ex)
{
    if (user_infos.empty() || !user_infos.front())
    {
        co_return;
    }
    Json::Value redis_root;
    for (const auto& user_info: user_infos)
    {
        if (!user_info)
        {
            continue;
        }
        redis_root["uids"].append(user_info->uid);
    }
    try
    {
        co_await set(USER_BASE_INFO_BY_NAME + user_infos.front()->name, redis_root.toStyledString());
        const auto success = co_await expire(USER_BASE_INFO_BY_NAME + user_infos.front()->name, ex);
        if (!success)
        {
            spdlog::warn("[redis] redis insert user_info failed!");
        }
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis insert user_info failed, error: {}", e.what());
    }
}

net::awaitable<void> RedisManager::insertMessageToOffline(chat_message::Message message)
{
    try
    {
        co_await streamAdd((message.is_to_group ? GROUP_OFFLINE_MESSAGE_STREAM : USER_OFFLINE_MESSAGE_STREAM) + std::to_string(message.to_uid),
                           {
                               {"from_uid", std::to_string(message.from_uid)},
                               {"type", std::to_string(message.type)},
                               {"body", message.body}
                           });
    }
    catch (std::exception& e)
    {
        spdlog::warn("[redis] redis insert message to offline failed, error: {}", e.what());
    }
}

net::awaitable<void> RedisManager::ackAndDelChatMessages(const std::uint64_t to_uid, std::vector<std::string> message_ids)
{
    try
    {
        co_await streamAckDel(USER_OFFLINE_MESSAGE_STREAM + std::to_string(to_uid), USER_XGROUP_NAME + std::to_string(to_uid), std::move(message_ids));
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis ackAndDel chat message failed, error: {}", e.what());
    }
}

net::awaitable<std::vector<chat_message::Message> > RedisManager::getMessageFromOffline(const std::uint64_t to_uid, const std::int64_t count, const MessageReadMode mode) const
{
    const std::string consumer = std::to_string(to_uid);
    std::vector<chat_message::Message> return_value;
    std::vector<StreamResult> stream_results;
    try
    {
        stream_results = co_await streamReadGroup(USER_XGROUP_NAME + consumer, consumer, USER_OFFLINE_MESSAGE_STREAM + consumer, mode, count);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis get message from offlien failed, error: {}", e.what());
        co_return return_value;
    }
    if (stream_results.empty())
    {
        co_return return_value;
    }
    for (const auto& [id, fields]: stream_results.front().entries)
    {
        chat_message::Message message;
        message.message_id = id;
        for (const auto& [key, value]: fields)
        {
            if (key == "from_uid") message.from_uid = std::stoull(value);
            else if (key == "type") message.type = static_cast<chat_message::Type>(std::stoi(value));
            else if (key == "body") message.body = value;
        }
        return_value.push_back(message);
    }
    co_return return_value;
}

net::awaitable<std::int64_t> RedisManager::getPendingMessageCount(const std::uint64_t uid) const
{
    PendingSummary pending_summary;
    try
    {
        pending_summary = co_await streamPending(USER_OFFLINE_MESSAGE_STREAM + std::to_string(uid), USER_XGROUP_NAME + std::to_string(uid));
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis get pending message failed, error: {}", e.what());
        co_return 0;
    }
    co_return pending_summary.totalCount;
}

net::awaitable<std::optional<std::string> > RedisManager::isUserOnLine(const std::uint64_t uid)
{
    std::optional<std::string> ip;
    try
    {
        ip = co_await get(USER_IP_PERFIX + std::to_string(uid));
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis check user online failed, error: {}", e.what());
        co_return std::nullopt;
    }
    co_return ip;
}

net::awaitable<bool> RedisManager::isFriend(const std::uint64_t online_uid, const std::uint64_t other_uid)
{
    static constexpr std::int64_t live_sencond = 7 * 24 * 60 * 60;
    const auto set_name = USER_FRIEND_SET + std::to_string(online_uid);
    const auto other_str = std::to_string(other_uid);
    bool is_friend{false};
    try
    {
        is_friend = co_await setIsMember(set_name, other_str);
        co_await expire(set_name, live_sencond);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis check user is friend failed, error: {}", e.what());
        co_return false;
    }
    co_return is_friend;
}

net::awaitable<bool> RedisManager::addFriend(const std::uint64_t online_uid, const std::uint64_t other_uid)
{
    const auto uidA_str = std::to_string(online_uid);
    const auto set_name_A = USER_FRIEND_SET + uidA_str;
    const auto uidB_str = std::to_string(other_uid);
    const auto set_name_B = USER_FRIEND_SET + uidB_str;
    bool success{false};
    try
    {
        // SADD 返回新增数量，0 表示"已经是好友"，不能当成失败
        const bool set_b_exists = co_await exists(set_name_B);
        co_await setAdd(set_name_A, {uidB_str});
        if (set_b_exists)
        {
            co_await setAdd(set_name_B, {uidA_str});
        }
        success = true;
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis add friend failed, error: {}", e.what());
        co_return false;
    }
    co_return success;
}

net::awaitable<bool> RedisManager::removeFriend(const std::uint64_t online_uid, const std::uint64_t other_uid)
{
    const auto uidA_str = std::to_string(online_uid);
    const auto set_name_A = USER_FRIEND_SET + uidA_str;
    const auto uidB_str = std::to_string(other_uid);
    const auto set_name_B = USER_FRIEND_SET + uidB_str;
    bool success{false};
    try
    {
        // SREM 返回删除数量，0 表示"本来就不在集合里"，不能当成失败
        const bool set_b_exists = co_await exists(set_name_B);
        co_await setRemove(set_name_A, {uidB_str});
        if (set_b_exists)
        {
            co_await setRemove(set_name_B, {uidA_str});
        }
        success = true;
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis remove friend failed, error: {}", e.what());
        co_return false;
    }
    co_return success;
}

net::awaitable<bool> RedisManager::loadFriends(const std::uint64_t online_uid, const std::vector<std::uint64_t> friend_uids)
{
    if (friend_uids.empty())
    {
        co_return true;
    }
    static constexpr long live_sencond = 7 * 24 * 60 * 60;
    std::vector<std::string> loaded_uids;
    loaded_uids.reserve(friend_uids.size());
    for (const auto& uid: friend_uids)
    {
        loaded_uids.push_back(std::to_string(uid));
    }
    bool success{false};
    try
    {
        // 好友集合可能已经存在，SADD 返回 0 也是成功；真正需要校验的是续期是否生效
        co_await setAdd(USER_FRIEND_SET + std::to_string(online_uid), std::move(loaded_uids));
        success = co_await expire(USER_FRIEND_SET + std::to_string(online_uid), live_sencond);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[redis] redis user load friends failed, error: {}", e.what());
        co_return false;
    }
    co_return success;
}

net::awaitable<void> RedisManager::set(std::string key, std::string value)
{
    co_return co_await redisCallWithoutReturn<>("SET", std::move(key), std::move(value));
}

net::awaitable<std::optional<std::string> > RedisManager::get(std::string key)
{
    co_return co_await redisCall<std::optional<std::string> >("GET", std::move(key));
}

net::awaitable<int64_t> RedisManager::hSet(std::string key, std::string field, std::string value)
{
    co_return co_await redisCall<int64_t>("HSET", std::move(key), std::move(field), std::move(value));
}

net::awaitable<std::optional<std::string> > RedisManager::hGet(std::string key, std::string field)
{
    co_return co_await redisCall<std::optional<std::string> >("HGET", std::move(key), std::move(field));
}

net::awaitable<int64_t> RedisManager::del(const std::vector<std::string> keys)
{
    co_return co_await redisCallRange<int64_t>("DEL", keys);
}

// EXISTS/EXPIRE/HEXISTS 回包是 RESP3 整数（:1），不能解析成 bool：
// boost.redis 的 bool 适配器只认 #t/#f，会把 "1" 判成 false，所以统一按整数取再转 bool
net::awaitable<bool> RedisManager::exists(std::string key)
{
    co_return co_await redisCall<int64_t>("EXISTS", std::move(key)) > 0;
}

net::awaitable<bool> RedisManager::expire(std::string key, const std::int64_t seconds)
{
    co_return co_await redisCall<int64_t>("EXPIRE", std::move(key), seconds) > 0;
}

net::awaitable<bool> RedisManager::hExists(std::string key, std::string field)
{
    co_return co_await redisCall<int64_t>("HEXISTS", std::move(key), std::move(field)) > 0;
}

net::awaitable<int64_t> RedisManager::hDel(std::string key, const std::vector<std::string> fields)
{
    co_return co_await redisCallRange<int64_t>("HDEL", std::move(key), fields);
}

net::awaitable<std::int64_t> RedisManager::setAdd(std::string key, const std::vector<std::string> members)
{
    co_return co_await redisCallRange<int64_t>("SADD", std::move(key), members);
}

net::awaitable<std::int64_t> RedisManager::setRemove(std::string key, const std::vector<std::string> members)
{
    co_return co_await redisCallRange<int64_t>("SREM", std::move(key), members);
}

net::awaitable<bool> RedisManager::setIsMember(std::string key, std::string member)
{
    co_return co_await redisCall<int64_t>("SISMEMBER", std::move(key), std::move(member));
}

net::awaitable<std::string> RedisManager::streamAdd(std::string stream_name, std::vector<std::pair<std::string, std::string> > field_values, const bool make_stream)
{
    std::vector<std::string> vector;
    if (!make_stream)
    {
        vector.reserve(field_values.size() * 2 + 2);
        vector.emplace_back("NOMKSTREAM");
    }
    else
    {
        vector.reserve(field_values.size() * 2 + 1);
    }
    vector.emplace_back("*");
    for (auto& [field, value]: field_values)
    {
        vector.push_back(std::move(field));
        vector.push_back(std::move(value));
    }
    co_return co_await redisCallRange<std::string>("XADD", std::move(stream_name), vector);
}

net::awaitable<void> RedisManager::streamCreateGroup(std::string stream_name, std::string group_name, const std::int64_t start_at, const bool make_stream) const
{
    std::vector<std::string> args;
    args.reserve(5);
    args.emplace_back("CREATE");
    args.emplace_back(std::move(stream_name));
    args.emplace_back(std::move(group_name));
    args.emplace_back(start_at == -1 ? "$" : std::to_string(start_at));
    if (make_stream) args.emplace_back("MKSTREAM");

    redis::request req;
    req.push_range("XGROUP", args.begin(), args.end());

    redis::response<std::string> resp;
    try
    {
        co_await m_connection->async_exec(
            req, resp, net::cancel_after(std::chrono::seconds(5)));
    }
    catch (const boost::system::system_error& e)
    {
        throw std::runtime_error(std::string("XGROUP CREATE conn/timeout: ") + e.what());
    }

    const auto& node = std::get<0>(resp);
    if (node.has_error())
    {
        if (node.error().diagnostic.find("BUSYGROUP") != std::string::npos)
        {
            co_return;
        }
        throw std::runtime_error("XGROUP CREATE failed: " + node.error().diagnostic);
    }
    co_return;
}

net::awaitable<std::vector<StreamResult> > RedisManager::streamReadGroup(std::string group_name, std::string consumer, std::string stream_name, const MessageReadMode mode, const int64_t count) const
{
    std::vector<std::string> args;
    args.reserve(8);
    args.emplace_back("GROUP");
    args.emplace_back(std::move(group_name));
    args.emplace_back(std::move(consumer));
    args.emplace_back("COUNT");
    args.emplace_back(std::to_string(count));
    args.emplace_back("STREAMS");
    args.emplace_back(std::move(stream_name));
    args.emplace_back(mode == MessageReadMode::New ? ">" : "0");
    redis::request req;
    req.push_range("XREADGROUP", std::begin(args), std::end(args));
    redis::generic_response resp;
    try
    {
        co_await m_connection->async_exec(req, resp, net::cancel_after(std::chrono::seconds(5)));
    }
    catch (const boost::system::system_error& e)
    {
        throw std::runtime_error(std::string("XREADGROUP error: ") + e.what());
    }
    // 1. 检查 result 包装器本身是否包含错误
    if (!resp.has_value())
    {
        // 这里可以处理 result 级别的错误（如解析失败等）
        throw std::runtime_error("XREADGROUP result error: " + resp.error().diagnostic);
    }
    // 2. 获取底层的 vector<resp3::node>
    const auto& nodes = resp.value();
    // 3. 检查节点级别的错误（RESP3 错误会作为节点出现）
    if (!nodes.empty())
    {
        const auto t = nodes.front().data_type;
        if (t == redis::resp3::type::simple_error
            || t == redis::resp3::type::blob_error)
        {
            throw std::runtime_error(
                "XREADGROUP failed: " + nodes.front().value);
        }
    }
    co_return Parse_xreadgroup_reply(resp);
}

net::awaitable<std::vector<std::int64_t> > RedisManager::streamAckDel(std::string stream_name, std::string group_name, std::vector<std::string> ids, std::string policy)
{
    std::vector<std::string> args;
    args.reserve(ids.size() + 5);
    args.emplace_back(std::move(stream_name));
    args.emplace_back(std::move(group_name));
    args.emplace_back(std::move(policy));
    args.emplace_back("IDS");
    args.emplace_back(std::to_string(ids.size()));
    for (auto& id: ids) args.emplace_back(std::move(id));
    co_return co_await redisCallRange<std::vector<std::int64_t> >("XACKDEL", args);
}

net::awaitable<PendingSummary> RedisManager::streamPending(const std::string stream_name, const std::string group_name) const
{
    // 对应 [总数, 最小ID, 最大ID, [[消费者, 数量], ...]]
    // 注意：boost.redis 1.92 的 response<std::tuple<...>> 适配器无法编译（wrapper<T> 不完整），
    // 这里与 XREADGROUP 一样用 generic_response 手工解析
    redis::request req;
    req.push("XPENDING", stream_name, group_name); // 不带范围参数，查摘要

    redis::generic_response resp;
    try
    {
        co_await m_connection->async_exec(req, resp, net::cancel_after(std::chrono::seconds(5)));
    }
    catch (const boost::system::system_error& e)
    {
        throw std::runtime_error(std::string("redis error: ") + e.what());
    }

    if (!resp.has_value())
    {
        throw std::runtime_error("XPENDING result error: " + resp.error().diagnostic);
    }

    PendingSummary result{};
    const auto& nodes = resp.value();
    if (nodes.empty() || nodes.front().data_type == redis::resp3::type::null)
    {
        co_return result;
    }

    const auto& root = nodes.front();
    if (!redis::resp3::is_aggregate(root.data_type))
    {
        throw std::runtime_error("XPENDING failed: " + std::string(root.value));
    }
    if (root.aggregate_size < 3)
    {
        co_return result; // 没有 pending 时 Redis 返回 [0, null, null, null]
    }

    std::size_t pos = 1; // nodes[0] 是根节点，其子节点紧随其后
    result.totalCount = nodes[pos].data_type == redis::resp3::type::null
                            ? 0
                            : std::stoll(std::string(nodes[pos].value));
    ++pos;
    if (nodes[pos].data_type != redis::resp3::type::null) result.smallestId = std::string(nodes[pos].value);
    ++pos;
    if (nodes[pos].data_type != redis::resp3::type::null) result.largestId = std::string(nodes[pos].value);
    ++pos;

    // 消费者明细：[[消费者, 数量], ...]
    if (pos >= nodes.size() || !redis::resp3::is_aggregate(nodes[pos].data_type))
    {
        co_return result;
    }
    const std::size_t consumer_count = nodes[pos].aggregate_size;
    ++pos;
    result.consumers.reserve(consumer_count);
    for (std::size_t i = 0; i < consumer_count && pos < nodes.size(); ++i)
    {
        if (!redis::resp3::is_aggregate(nodes[pos].data_type))
        {
            break;
        }
        ++pos; // 进入该消费者的 [name, count] 数组
        ConsumerPendingInfo info;
        info.consumerName = std::string(nodes[pos].value);
        ++pos;
        info.pendingCount = std::stoll(std::string(nodes[pos].value));
        ++pos;
        result.consumers.push_back(std::move(info));
    }

    co_return result;
}

RedisManager::RedisManager() = default;

void RedisManager::init(net::io_context& io_context)
{
    if (m_connection)
    {
        spdlog::warn("[redis] init() called more than once, ignore");
        return;
    }
    m_connection = std::make_shared<redis::connection>(io_context);
    redis::config redis_config;
    auto& config = ConfigManager::GetInstance();
    redis_config.addr.port = config["Redis"]["port"];
    redis_config.addr.host = config["Redis"]["host"];
    redis_config.password = config["Redis"]["passwd"];
    redis_config.database_index = 0;
    redis_config.use_ssl = false;

    m_connection->async_run(redis_config, net::detached);
}

RedisManager::RedisManager(net::io_context& io_context)
{
    init(io_context);
}
