#include "ServerRegistry.h"

#include <chrono>
#include <string>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <spdlog/spdlog.h>

#include <RedisManager.h>
#include <const.h>

ServerRegistry::~ServerRegistry()
{
    stop();
}

void ServerRegistry::start(boost::asio::io_context& io_context, std::string name, std::string address)
{
    if (m_started.exchange(true))
    {
        spdlog::warn("[ServerRegistry] start() called more than once, ignore");
        return;
    }

    m_ioContext = &io_context;
    m_name = std::move(name);
    m_address = std::move(address);
    m_heartbeatKey = SERVER_HEARTBEAT_PREFIX + m_name;
    m_countKey = LOGIN_COUNT_PREFIX + m_name;
    m_timer = std::make_unique<boost::asio::steady_timer>(io_context);

    boost::asio::co_spawn(io_context, renewLoop(), boost::asio::detached);
}

void ServerRegistry::stop()
{
    if (m_stop.exchange(true))
    {
        return; // 已被 stop() 处理过，避免重复摘除
    }
    if (!m_started.load() || m_ioContext == nullptr)
    {
        return; // 从未注册过，无需摘除
    }

    // 定时器属于逻辑线程：投递取消操作，避免跨线程操作同一个 timer；
    // 投递与下面的 co_spawn 在同一个 io_context 上按序执行，取消一定发生在摘除之前
    boost::asio::post(*m_ioContext, [this]()
                          {
                              if (m_timer)
                              {
                                  m_timer->cancel();
                              }
                          });

    try
    {
        // 同步等待摘除完成：调用方（main）随后就可能退出，Redis 连接不能先被销毁
        boost::asio::co_spawn(*m_ioContext, unregisterOnce(), boost::asio::use_future).get();
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[ServerRegistry] unregister from redis failed, name: {}, error: {}", m_name, e.what());
    }
}

void ServerRegistry::addConnection()
{
    ++m_connectionCount;
    if (m_started.load() && !m_stop.load() && m_ioContext != nullptr)
    {
        boost::asio::co_spawn(*m_ioContext, publishCount(), boost::asio::detached);
    }
}

void ServerRegistry::removeConnection()
{
    // CAS 自旋递减，保证并发断开时不会减到 0 以下
    auto current = m_connectionCount.load();
    while (current > 0 && !m_connectionCount.compare_exchange_weak(current, current - 1))
    {
    }
    if (m_started.load() && !m_stop.load() && m_ioContext != nullptr)
    {
        boost::asio::co_spawn(*m_ioContext, publishCount(), boost::asio::detached);
    }
}

boost::asio::awaitable<bool> ServerRegistry::publish()
{
    auto& redis = RedisManager::GetInstance();
    try
    {
        // 服务列表：字段常驻，不设 TTL，便于 StatusServer 按名字反查地址
        co_await redis.hSet(SERVER_LIST, m_name, m_address);
        // 存活标记：EX 30s，续期协程每 10s 刷新
        co_await redis.set(m_heartbeatKey, m_address);
        co_await redis.expire(m_heartbeatKey, HEART_BEAT_EXPIRE_SECONDS);
        // 连接数：同样 EX 30s，实例死掉后自动消失，StatusServer 视为 0
        co_await redis.set(m_countKey, std::to_string(m_connectionCount.load()));
        co_await redis.expire(m_countKey, HEART_BEAT_EXPIRE_SECONDS);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[ServerRegistry] publish to redis failed, name: {}, error: {}", m_name, e.what());
        co_return false;
    }
    co_return true;
}

boost::asio::awaitable<void> ServerRegistry::publishCount()
{
    try
    {
        co_await RedisManager::GetInstance().set(m_countKey, std::to_string(m_connectionCount.load()));
        co_await RedisManager::GetInstance().expire(m_countKey, HEART_BEAT_EXPIRE_SECONDS);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[ServerRegistry] publish connection count failed, name: {}, error: {}", m_name, e.what());
    }
}

boost::asio::awaitable<void> ServerRegistry::unregisterOnce()
{
    try
    {
        auto& redis = RedisManager::GetInstance();
        // 优雅退出：立即摘除，不等 30s TTL 自然过期
        co_await redis.del({m_heartbeatKey, m_countKey});
        co_await redis.hDel(SERVER_LIST, {m_name});
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[ServerRegistry] unregister from redis failed, name: {}, error: {}", m_name, e.what());
    }
}

boost::asio::awaitable<void> ServerRegistry::renewLoop()
{
    if (!co_await publish())
    {
        // 注册失败不阻断启动：续期循环会周期性重试，Redis 恢复后自动补上
        spdlog::warn("[ServerRegistry] register self to redis failed, name: {}", m_name);
    }

    for (;;)
    {
        m_timer->expires_after(std::chrono::seconds(SERVER_HEARTBEAT_RENEW_SECONDS));
        try
        {
            co_await m_timer->async_wait(boost::asio::use_awaitable);
        }
        catch (const boost::system::system_error& e)
        {
            // stop() 取消了定时器：退出续期循环
            spdlog::debug("[ServerRegistry] renew loop stop, name: {}, error: {}", m_name, e.what());
            co_return;
        }

        if (m_stop.load())
        {
            co_return;
        }
        if (!co_await publish())
        {
            spdlog::warn("[ServerRegistry] renew heartbeat failed, name: {}", m_name);
        }
    }
}
