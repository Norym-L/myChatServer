#ifndef GATESERVER_SERVERREGISTRY_H
#define GATESERVER_SERVERREGISTRY_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "Singleton.h"

/// ChatServer 向 Redis 注册自身的存活与负载信息，供 StatusServer 分发时参考：
///  - hash  SERVER_LIST：字段 name，值 host:port（服务列表，长期保存，不设 TTL）
///  - key   serverheartbeat_<name>：EX 30s 的存活标记
///  - key   logincount_<name>：EX 30s 的当前连接数
///
/// 后两个 key 由后台线程每 10s 续期一次：进程退出/崩溃后自动过期，不会残留脏数据。
/// 连接数由本类持有（原子计数），登录 +1、断开 -1，续期时按当前值整体写回，
/// 因此即使 Redis 被清空或 key 过期，也能在下一次续期自动恢复。
class ServerRegistry : public TSingLeton<ServerRegistry>
{
    friend class TSingLeton;

public:
    ~ServerRegistry();

    /// 启动注册与续期线程；重复调用只有第一次生效。
    /// name 需与配置 [ChatServerN].name 一致，address 为对外地址 host:port
    void start(std::string name, std::string address);

    /// 停止续期并摘除 Redis 记录；幂等，须在 RedisManager 关闭前调用
    void stop();

    /// 登录成功时调用，本实例连接数 +1
    void addConnection();
    /// 连接断开时调用，本实例连接数 -1（不会低于 0）
    void removeConnection();

private:
    ServerRegistry() = default;

    void renewLoop();

    /// 写服务列表 + 存活 key + 连接数 key
    [[nodiscard]] bool publish() const;
    /// 只刷新连接数 key（登录/断开时立即更新，避免续期间隔内读到旧值）
    void publishCount() const;
    /// 删除存活 key、连接数 key 与服务列表字段
    void unregisterOnce() const;

    std::string m_name;
    std::string m_address;
    std::string m_heartbeatKey;
    std::string m_countKey;
    std::atomic<std::uint64_t> m_connectionCount{0};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stop{false};
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_thread;
};

#endif //GATESERVER_SERVERREGISTRY_H
