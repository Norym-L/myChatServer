#include "ServerRegistry.h"

#include <chrono>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <RedisManager.h>
#include <const.h>

ServerRegistry::~ServerRegistry()
{
    stop();
}

void ServerRegistry::start(std::string name, std::string address)
{
    if (m_started.exchange(true))
    {
        spdlog::warn("[ServerRegistry] start() called more than once, ignore");
        return;
    }

    m_name = std::move(name);
    m_address = std::move(address);
    m_heartbeatKey = SERVER_HEARTBEAT_PREFIX + m_name;
    m_countKey = LOGIN_COUNT_PREFIX + m_name;

    if (!publish())
    {
        // 注册失败不阻断启动：续期线程会周期性重试，Redis 恢复后自动补上
        spdlog::warn("[ServerRegistry] register self to redis failed, name: {}", m_name);
    }
    m_thread = std::thread(&ServerRegistry::renewLoop, this);
}

void ServerRegistry::stop()
{
    if (m_stop.exchange(true))
    {
        return; // 已被 stop() 处理过，避免重复 join / 重复摘除
    }
    m_condition.notify_all();
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    unregisterOnce();
}

void ServerRegistry::addConnection()
{
    ++m_connectionCount;
    if (m_started.load())
    {
        publishCount();
    }
}

void ServerRegistry::removeConnection()
{
    // CAS 自旋递减，保证并发断开时不会减到 0 以下
    auto current = m_connectionCount.load();
    while (current > 0 && !m_connectionCount.compare_exchange_weak(current, current - 1))
    {
    }
    if (m_started.load())
    {
        publishCount();
    }
}

bool ServerRegistry::publish() const
{
    // 服务列表：字段常驻，不设 TTL，便于 StatusServer 按名字反查地址
    const bool listed = RedisManager::GetInstance().hSet(
        SERVER_LIST, m_name.c_str(), m_address.c_str(), m_address.size());
    // 存活标记：EX 30s，续期线程每 10s 刷新
    const bool alive = RedisManager::GetInstance().set(
        m_heartbeatKey, m_address, HEART_BEAT_EXPIRE_SECONDS);
    // 连接数：同样 EX 30s，实例死掉后自动消失，StatusServer 视为 0
    const bool counted = RedisManager::GetInstance().set(
        m_countKey, std::to_string(m_connectionCount.load()), HEART_BEAT_EXPIRE_SECONDS);
    return listed && alive && counted;
}

void ServerRegistry::publishCount() const
{
    (void) RedisManager::GetInstance().set(
        m_countKey, std::to_string(m_connectionCount.load()), HEART_BEAT_EXPIRE_SECONDS);
}

void ServerRegistry::unregisterOnce() const
{
    if (!m_started.load())
    {
        return; // 从未注册过，无需摘除
    }
    // 优雅退出：立即摘除，不等 30s TTL 自然过期
    (void) RedisManager::GetInstance().del(m_heartbeatKey);
    (void) RedisManager::GetInstance().del(m_countKey);
    (void) RedisManager::GetInstance().hDel(SERVER_LIST, m_name);
}

void ServerRegistry::renewLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_stop.load())
    {
        // 有 stop 唤醒或等满一个周期都返回，随后统一判断退出条件
        m_condition.wait_for(lock, std::chrono::seconds(SERVER_HEARTBEAT_RENEW_SECONDS),
                             [this]() { return m_stop.load(); });
        if (m_stop.load())
        {
            break;
        }
        if (!publish())
        {
            spdlog::warn("[ServerRegistry] renew heartbeat failed, name: {}", m_name);
        }
    }
}
