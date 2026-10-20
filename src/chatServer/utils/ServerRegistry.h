#ifndef GATESERVER_SERVERREGISTRY_H
#define GATESERVER_SERVERREGISTRY_H

#include <atomic>
#include <memory>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "Singleton.h"

/// ChatServer 向 Redis 注册自身的存活与负载信息，供 StatusServer 分发时参考：
///  - hash  SERVER_LIST：字段 name，值 host:port（服务列表，长期保存，不设 TTL）
///  - key   serverheartbeat_<name>：EX 30s 的存活标记
///  - key   logincount_<name>：EX 30s 的当前连接数
///
/// 后两个 key 由逻辑线程上的定时协程每 10s 续期一次：进程退出/崩溃后自动过期，不会残留脏数据。
/// 连接数由本类持有（原子计数），登录 +1、断开 -1，续期时按当前值整体写回，
/// 因此即使 Redis 被清空或 key 过期，也能在下一次续期自动恢复。
class ServerRegistry : public TSingLeton<ServerRegistry>
{
    friend class TSingLeton;

public:
    ~ServerRegistry();

    /// 启动注册与续期；重复调用只有第一次生效。
    /// io_context 必须是承载 Redis 连接的那条逻辑线程的 io_context，
    /// name 需与配置 [ChatServerN].name 一致，address 为对外地址 host:port
    void start(boost::asio::io_context& io_context, std::string name, std::string address);

    /// 停止续期并摘除 Redis 记录；幂等，须在 RedisManager 关闭前调用
    void stop();

    /// 登录成功时调用，本实例连接数 +1
    void addConnection();
    /// 连接断开时调用，本实例连接数 -1（不会低于 0）
    void removeConnection();

private:
    ServerRegistry() = default;

    boost::asio::awaitable<void> renewLoop();

    /// 写服务列表 + 存活 key + 连接数 key
    boost::asio::awaitable<bool> publish();
    /// 只刷新连接数 key（登录/断开时立即更新，避免续期间隔内读到旧值）
    boost::asio::awaitable<void> publishCount();
    /// 删除存活 key、连接数 key 与服务列表字段
    boost::asio::awaitable<void> unregisterOnce();

    boost::asio::io_context* m_ioContext = nullptr;
    std::unique_ptr<boost::asio::steady_timer> m_timer;
    std::string m_name;
    std::string m_address;
    std::string m_heartbeatKey;
    std::string m_countKey;
    std::atomic<std::uint64_t> m_connectionCount{0};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stop{false};
};

#endif //GATESERVER_SERVERREGISTRY_H
