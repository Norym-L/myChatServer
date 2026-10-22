#ifndef GATESERVER_REDISMANAGER_BOOST_H
#define GATESERVER_REDISMANAGER_BOOST_H

#include <boost/redis/connection.hpp>
#include <boost/redis/request.hpp>
#include <boost/redis/response.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <string>
#include <optional>
#include <thread>

#include <Singleton.h>

#include "data/UserInfo.h"

struct UserInfo;

namespace chat_message
{
struct Message;
}

namespace redis = boost::redis;
namespace net = boost::asio;

// 单条 Stream 消息
struct StreamEntry
{
    std::string id;                                           // 消息 ID
    std::vector<std::pair<std::string, std::string> > fields; // 字段-值对（保持顺序）
};

// 单个 Stream 的读取结果
struct StreamResult
{
    std::string streamName;           // 流名
    std::vector<StreamEntry> entries; // 该流返回的消息列表
};

// 消费者持有的 pending 数量
struct ConsumerPendingInfo
{
    std::string consumerName;
    std::int64_t pendingCount;
};

// XPENDING 的总体摘要
struct PendingSummary
{
    std::int64_t totalCount;
    std::string smallestId;
    std::string largestId;
    std::vector<ConsumerPendingInfo> consumers;
};

/// XREADGROUP 的读取位置
enum class MessageReadMode
{
    New,    // ">": 只取从未投递的新消息
    Pending // "0": 读取当前 consumer（uid）的 PEL，即已投递但未 ACK 的消息
};

class RedisManager : public TSingLeton<RedisManager>
{
    friend class TSingLeton;

public:
    [[nodiscard]] net::awaitable<bool> registUser(std::uint64_t uid, std::string server_name);
    net::awaitable<void> insert(std::vector<std::shared_ptr<UserInfo> > user_infos, long ex = 7 * 24 * 60 * 60);
    [[nodiscard]] net::awaitable<std::optional<UserInfo> > getUserInfo(std::uint64_t uid);
    net::awaitable<void> setUserBaseInfo(std::uint64_t uid, UserInfo user_info);
    net::awaitable<void> insertMessageToOffline(chat_message::Message message);
    net::awaitable<void> ackAndDelChatMessages(std::uint64_t to_uid, std::vector<std::string> message_ids);
    [[nodiscard]] net::awaitable<std::vector<chat_message::Message> > getMessageFromOffline(std::uint64_t to_uid, std::int64_t count = 1, MessageReadMode mode = MessageReadMode::New) const;
    [[nodiscard]] net::awaitable<std::int64_t> getPendingMessageCount(std::uint64_t uid) const;
    [[nodiscard]] net::awaitable<std::optional<std::string> > isUserOnLine(std::uint64_t uid);
    [[nodiscard]] net::awaitable<bool> isFriend(std::uint64_t online_uid, std::uint64_t other_uid);
    [[nodiscard]] net::awaitable<bool> addFriend(std::uint64_t online_uid, std::uint64_t other_uid);
    [[nodiscard]] net::awaitable<bool> removeFriend(std::uint64_t online_uid, std::uint64_t other_uid);
    [[nodiscard]] net::awaitable<bool> loadFriends(std::uint64_t online_uid, std::vector<std::uint64_t> friend_uids);
    [[nodiscard]] net::awaitable<bool> changeUserInfo(std::uint64_t uid, std::optional<std::string> name, std::optional<std::string> desc, std::optional<std::string> icon, std::optional<Sex> sex);
    net::awaitable<void> insertToGroup(std::uint64_t group_id, std::vector<std::string> uids);         // 添加好友到群聊,没有群聊就创建群聊
    [[nodiscard]] net::awaitable<std::vector<std::string> > getUsersFromGroup(std::uint64_t group_id); // 获取群友
    [[nodiscard]] net::awaitable<bool> kickUserFromGroup(std::uint64_t group_id, std::string uid);     // 把人踢出群聊
    [[nodiscard]] net::awaitable<bool> removeGroup(std::uint64_t group_id);                            // 删除解散群聊
    [[nodiscard]] net::awaitable<bool> isInGroup(std::uint64_t group_id, std::uint64_t uid);           // 是否在群里

    [[nodiscard]] net::awaitable<int64_t> del(std::vector<std::string> keys);
    [[nodiscard]] net::awaitable<bool> exists(std::string key);
    [[nodiscard]] net::awaitable<bool> expire(std::string key, std::int64_t seconds);
    // key - value
    net::awaitable<void> set(std::string key, std::string value, std::int64_t ex = 7 * 24 * 60 * 60);
    [[nodiscard]] net::awaitable<std::optional<std::string> > get(std::string key);
    // key - h_key - value
    [[nodiscard]] net::awaitable<int64_t> hSet(std::string key, std::string field, std::string value);
    [[nodiscard]] net::awaitable<std::optional<std::string> > hGet(std::string key, std::string field);
    [[nodiscard]] net::awaitable<bool> hExists(std::string key, std::string field);
    [[nodiscard]] net::awaitable<int64_t> hDel(std::string key, std::vector<std::string> fields);
    // set key - (value1, value2 ...)
    [[nodiscard]] net::awaitable<std::int64_t> setAdd(std::string key, std::vector<std::string> members);
    [[nodiscard]] net::awaitable<std::int64_t> setRemove(std::string key, std::vector<std::string> members);
    [[nodiscard]] net::awaitable<std::vector<std::string> > setMembers(std::string key);
    [[nodiscard]] net::awaitable<bool> setIsMember(std::string key, std::string member);

    // stream key - stream
    [[nodiscard]] net::awaitable<std::string> streamAdd(std::string stream_name, std::vector<std::pair<std::string, std::string> > field_values, bool make_stream = true);
    [[nodiscard]] net::awaitable<std::vector<std::string> > streamsAdd(std::vector<std::string> stream_names, std::vector<std::vector<std::pair<std::string, std::string> > > field_values, bool make_stream = true);
    net::awaitable<void> streamCreateGroup(std::string stream_name, std::string group_name, std::int64_t start_at = 0, bool make_stream = true) const;
    [[nodiscard]] net::awaitable<std::vector<StreamResult> > streamReadGroup(std::string group_name, std::string consumer, std::string stream_name, MessageReadMode mode = MessageReadMode::New, int64_t count = 1) const;
    // policy:  "KEEPREF" | "DELREF" | "ACKED" 返回值：每个id的情况 1 删除， -1 不存在， 2 确认但未删除
    [[nodiscard]] net::awaitable<std::vector<std::int64_t> > streamAckDel(std::string stream_name, std::string group_name, std::vector<std::string> ids, std::string policy = "ACKED");
    [[nodiscard]] net::awaitable<PendingSummary> streamPending(std::string stream_name, std::string group_name) const;

    /// 使用调用方提供的 io_context（如 ChatServer 的逻辑线程）：业务协程与 Redis 命令跑在同一条线程上
    void init(net::io_context& io_context);
    /// 自建 io_context + 单线程（供没有逻辑线程的 GateServer / StatusServer 使用）
    void init();

    /// 同步等待一个 Redis 协程的结果：仅用于没有协程上下文的同步调用方（如 gRPC 同步处理器），
    /// 会阻塞当前线程直到命令完成，因此不要在 Redis 线程或逻辑线程上调用
    template<class T>
    T wait(net::awaitable<T> op)
    {
        if (m_ioContext == nullptr || !m_thread.joinable())
        {
            throw std::runtime_error("[redis] wait() requires RedisManager::init() with a dedicated thread");
        }
        if (m_ioContext->get_executor().running_in_this_thread())
        {
            throw std::runtime_error("[redis] wait() must not be called on the redis thread");
        }
        return net::co_spawn(*m_ioContext, std::move(op), net::use_future).get();
    }

private:
    RedisManager();
    ~RedisManager();

    explicit RedisManager(net::io_context& io_context);
    std::shared_ptr<redis::connection> m_connection;

    net::io_context m_ownIoContext; // 自建线程时使用的 io_context
    net::executor_work_guard<net::io_context::executor_type> m_workGuard{net::make_work_guard(m_ownIoContext)};
    net::io_context* m_ioContext = nullptr; // 实际承载连接的 io_context
    std::thread m_thread;                   // 自建线程（仅 init() 无参版本使用）

    /// 当前线程是否为承载 Redis 连接的那条线程。
    /// 连接不是线程安全的，所有 async_exec 都必须在它的 io_context 线程上发起
    [[nodiscard]] bool onRedisThread() const
    {
        return m_ioContext == nullptr || m_ioContext->get_executor().running_in_this_thread();
    }

    [[nodiscard]] bool initialized() const noexcept { return m_connection != nullptr; }

    // 所有redis连接走独立串行执行
    template<class ExecOp>
    net::awaitable<void> execOnRedisStrand(ExecOp&& exec_op) const
    {
        co_return co_await net::co_spawn(m_connection->get_executor(), std::forward<ExecOp>(exec_op), net::use_awaitable);
    }

    template<class ReturnType, class... Args>
    net::awaitable<ReturnType> redisCall(Args&&... args)
    {
        if (!onRedisThread())
        {
            // 从其他线程调用：切到 Redis 线程执行，执行完再切回来
            co_return co_await net::co_spawn(*m_ioContext,
                                             redisCallOnRedisThread<ReturnType>(std::forward<Args>(args)...),
                                             net::use_awaitable);
        }
        co_return co_await redisCallOnRedisThread<ReturnType>(std::forward<Args>(args)...);
    }

    template<class ReturnType, class Range>
    net::awaitable<ReturnType> redisCallRange(std::string cmd, const Range& args)
    {
        if (!onRedisThread())
        {
            co_return co_await net::co_spawn(*m_ioContext,
                                             redisCallRangeOnRedisThread<ReturnType>(std::move(cmd), args),
                                             net::use_awaitable);
        }
        co_return co_await redisCallRangeOnRedisThread<ReturnType>(std::move(cmd), args);
    }

    template<class ReturnType, class Range>
    net::awaitable<ReturnType> redisCallRange(std::string cmd, std::string key, const Range& args)
    {
        if (!onRedisThread())
        {
            co_return co_await net::co_spawn(
                *m_ioContext,
                redisCallRangeOnRedisThread<ReturnType>(std::move(cmd), std::move(key), args),
                net::use_awaitable);
        }
        co_return co_await redisCallRangeOnRedisThread<ReturnType>(std::move(cmd), std::move(key), args);
    }

    template<class... Args>
    net::awaitable<void> redisCallWithoutReturn(Args&&... args)
    {
        if (!onRedisThread())
        {
            co_await net::co_spawn(*m_ioContext,
                                   redisCallWithoutReturnOnRedisThread(std::forward<Args>(args)...),
                                   net::use_awaitable);
            co_return;
        }
        co_await redisCallWithoutReturnOnRedisThread(std::forward<Args>(args)...);
    }

    template<class Range>
    net::awaitable<void> redisCallRangeWithoutReturn(std::string cmd, const Range& args)
    {
        if (!onRedisThread())
        {
            co_await net::co_spawn(*m_ioContext,
                                   redisCallRangeWithoutReturnOnRedisThread(std::move(cmd), args),
                                   net::use_awaitable);
            co_return;
        }
        co_await redisCallRangeWithoutReturnOnRedisThread(std::move(cmd), args);
    }

    // ---- 以下 *OnRedisThread 版本对调用线程没有要求：exec 统一经
    // execOnRedisStrand 派发到连接自己的 strand 上发起 ----

    template<class ReturnType, class... Args>
    net::awaitable<ReturnType> redisCallOnRedisThread(Args&&... args)
    {
        if (!initialized())
        {
            throw std::runtime_error("[redis] RedisManager is not initialized");
        }
        redis::request req;
        req.push(std::forward<Args>(args)...);
        redis::response<ReturnType> resp;
        try
        {
            co_await execOnRedisStrand([&]() -> net::awaitable<void>
                {
                    co_await m_connection->async_exec(req, resp, net::cancel_after(std::chrono::seconds(5)));
                });
        }
        catch (const boost::system::system_error& e)
        {
            throw std::runtime_error(std::string("redis error: ") + e.what());
        }
        auto& node = std::get<0>(resp);
        if (node.has_error())
            throw std::runtime_error("redis cmd failed: " + node.error().diagnostic);
        co_return node.value();
    }

    template<class ReturnType, class Range>
    net::awaitable<ReturnType> redisCallRangeOnRedisThread(std::string cmd, const Range& args)
    {
        if (!initialized())
        {
            throw std::runtime_error("[redis] RedisManager is not initialized");
        }
        redis::request req;
        req.push_range(cmd, std::begin(args), std::end(args));

        redis::response<ReturnType> resp;
        try
        {
            co_await execOnRedisStrand([&]() -> net::awaitable<void>
                {
                    co_await m_connection->async_exec(req, resp, net::cancel_after(std::chrono::seconds(5)));
                });
        }
        catch (const boost::system::system_error& e)
        {
            throw std::runtime_error(std::string("redis error: ") + e.what());
        }
        auto& node = std::get<0>(resp);
        if (node.has_error())
            throw std::runtime_error("redis cmd failed: " + node.error().diagnostic);
        co_return node.value();
    }

    template<class ReturnType, class Range>
    net::awaitable<ReturnType> redisCallRangeOnRedisThread(std::string cmd, std::string key, const Range& args)
    {
        if (!initialized())
        {
            throw std::runtime_error("[redis] RedisManager is not initialized");
        }
        redis::request req;
        req.push_range(cmd, key, std::begin(args), std::end(args));

        redis::response<ReturnType> resp;
        try
        {
            co_await execOnRedisStrand([&]() -> net::awaitable<void>
                {
                    co_await m_connection->async_exec(req, resp, net::cancel_after(std::chrono::seconds(5)));
                });
        }
        catch (const boost::system::system_error& e)
        {
            throw std::runtime_error(std::string("redis error: ") + e.what());
        }
        auto& node = std::get<0>(resp);
        if (node.has_error())
            throw std::runtime_error("redis cmd failed: " + node.error().diagnostic);
        co_return node.value();
    }

    template<class... Args>
    net::awaitable<void> redisCallWithoutReturnOnRedisThread(Args&&... args)
    {
        if (!initialized())
        {
            throw std::runtime_error("[redis] RedisManager is not initialized");
        }
        redis::request req;
        req.push(std::forward<Args>(args)...);
        try
        {
            co_await execOnRedisStrand([&]() -> net::awaitable<void>
                {
                    co_await m_connection->async_exec(req, redis::ignore, net::cancel_after(std::chrono::seconds(5)));
                });
        }
        catch (const boost::system::system_error& e)
        {
            throw std::runtime_error(std::string("redis error: ") + e.what());
        }
    }

    template<class Range>
    net::awaitable<void> redisCallRangeWithoutReturnOnRedisThread(std::string cmd, const Range& args)
    {
        if (!initialized())
        {
            throw std::runtime_error("[redis] RedisManager is not initialized");
        }
        redis::request req;
        req.push_range(cmd, std::begin(args), std::end(args));
        try
        {
            co_await execOnRedisStrand([&]() -> net::awaitable<void>
                {
                    co_await m_connection->async_exec(req, redis::ignore, net::cancel_after(std::chrono::seconds(5)));
                });
        }
        catch (const boost::system::system_error& e)
        {
            throw std::runtime_error(std::string("redis error: ") + e.what());
        }
    }

    static std::vector<StreamResult> Parse_xreadgroup_reply(const redis::generic_response& resp)
    {
        std::vector<StreamResult> results;

        // 1. 先检查 result 是否包含错误，如果没有值则返回空
        if (!resp.has_value())
        {
            // 这里可以根据需要记录日志或抛出异常，无消息时通常是空 vector 或 null
            return results;
        }

        // 2. 通过 .value() 获取底层的 std::vector<resp3::node>
        const auto& nodes = resp.value();

        // 3. 现在就可以安全地使用 .empty(), .front(), [] 或 .at() 了
        if (nodes.empty() || nodes.front().data_type == redis::resp3::type::null)
            return results;

        const auto& top = nodes[0]; // 或者用 nodes.at(0)
        if (!redis::resp3::is_aggregate(top.data_type))
            throw std::runtime_error("XREADGROUP: expected aggregate at root");

        const std::size_t stream_count = top.aggregate_size;
        std::size_t pos = 1;

        for (std::size_t s = 0; s < stream_count; ++s)
        {
            StreamResult sr;
            sr.streamName = nodes[pos].value; // 使用 nodes[pos]
            ++pos;

            const auto& entries_node = nodes[pos];
            const std::size_t entry_count = entries_node.aggregate_size;
            ++pos;

            sr.entries.reserve(entry_count);
            for (std::size_t e = 0; e < entry_count; ++e)
            {
                ++pos; // 每条消息自身是 [id, fields] 数组，先跳过该数组的头节点
                if (pos + 1 >= nodes.size())
                {
                    throw std::runtime_error("XREADGROUP: unexpected end of reply");
                }

                StreamEntry entry;
                entry.id = nodes[pos].value; // 使用 nodes[pos]
                ++pos;

                const auto& fields_node = nodes[pos];
                const std::size_t pair_count = fields_node.aggregate_size / 2;
                ++pos;

                entry.fields.reserve(pair_count);
                for (std::size_t p = 0; p < pair_count; ++p)
                {
                    std::string field = nodes[pos].value;
                    ++pos; // 使用 nodes[pos]
                    std::string value = nodes[pos].value;
                    ++pos; // 使用 nodes[pos]
                    entry.fields.emplace_back(std::move(field), std::move(value));
                }
                sr.entries.push_back(std::move(entry));
            }
            results.push_back(std::move(sr));
        }

        return results;
    }
};

#define CODE_PREFIX "code_"
#define USER_IP_PERFIX "uip_"
#define USER_TOKEN_PERFIX "utoken_"
#define IP_COUNT_PERFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define USER_BASE_INFO_BY_NAME "ubaseinfo_name_" // 需要处理多个值，连接用户uid数组
#define LOGIN_COUNT_PREFIX "logincount_" // 每实例连接数 key 前缀：logincount_<name>，EX 30s，随实例心跳续期

#define HEART_BEAT_EXPIRE_SECONDS 30 // 心跳续期时长（秒）：超过该时长未收到心跳，在线 key 过期，用户判定为离线

#define SERVER_LIST "serverlist"                    // hash：服务名 -> 对外地址(host:port)，服务发现用
#define SERVER_HEARTBEAT_PREFIX "serverheartbeat_"  // 存活 key 前缀：serverheartbeat_<name>，EX 30s 表示实例存活
#define SERVER_HEARTBEAT_RENEW_SECONDS 10           // 存活 key 的续期间隔（秒），必须显著小于 TTL，留出容错

#define USER_OFFLINE_MESSAGE_STREAM "offlinestream_" // 用户离线消息队列 + uid
// #define GROUP_OFFLINE_MESSAGE_STREAM ⚠ 群聊用户离线消息队列 + uid 废弃,把消息直接推送到用户的离线消息队列
#define USER_XGROUP_NAME "userxgroup_" // 用户聊天消息消费组 + uid

#define USER_FRIEND_SET "userfriendset_" // 用户好友集合 + uid --- uid->(uid1,uid2...)
#define CHAT_GROUP_PERFIX "chatgroup_" // 群聊编号前缀 + uid（群聊id）
#endif //GATESERVER_REDISMANAGER_BOOST_H
