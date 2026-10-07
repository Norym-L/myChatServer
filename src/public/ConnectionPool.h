#ifndef GATESERVER_CONNECTIONPOOL_H
#define GATESERVER_CONNECTIONPOOL_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

template<typename T>
class ConnectionPool
{
public:
    using Factory = std::function<std::unique_ptr<T>()>;

    ConnectionPool(Factory factory, const std::size_t max_pool_size, const std::chrono::milliseconds deadLineMs = std::chrono::milliseconds(5000))
        : ConnectionPool(std::move(factory), max_pool_size / 2, max_pool_size, deadLineMs)
    {}

    ConnectionPool(Factory factory, const std::size_t min_pool_size, const std::size_t max_pool_size, const std::chrono::milliseconds deadLineMs = std::chrono::milliseconds(5000))
        : m_factory(std::move(factory))
        , m_maxPoolSize(max_pool_size)
        , m_minPoolSize(min_pool_size)
        , m_currentSize(0)
        , m_deadLineMs(deadLineMs)
    {
        if (m_minPoolSize > m_maxPoolSize)
            m_minPoolSize = m_maxPoolSize;

        for (std::size_t i = 0; i < m_minPoolSize; ++i)
        {
            m_connections.emplace(m_factory());
            ++m_currentSize;
        }
    }

    ~ConnectionPool()
    {
        close();
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    [[nodiscard]] std::unique_ptr<T> getConnection()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 1. 优先复用空闲连接
        if (!m_connections.empty())
        {
            auto conn = std::move(m_connections.front());
            m_connections.pop();
            return conn;
        }

        // 2. 没有空闲，且未达上限，创建新连接
        if (m_currentSize < m_maxPoolSize)
        {
            ++m_currentSize;
            lock.unlock(); // 创建可能耗时，释放锁
            try
            {
                return m_factory();
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                --m_currentSize;
                throw;
            }
        }

        // 3. 已达上限，等待归还
        const bool ok = m_condition.wait_for(
            lock, m_deadLineMs,
            [this]() { return m_beStop || !m_connections.empty(); });

        if (!ok || m_beStop || m_connections.empty())
            return nullptr;

        auto conn = std::move(m_connections.front());
        m_connections.pop();
        return conn;
    }

    void returnConnection(std::unique_ptr<T> connection)
    {
        if (!connection) return;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_beStop) return;

            // 如果当前连接数超过最小值，销毁多余连接
            if (m_currentSize > m_minPoolSize)
            {
                --m_currentSize;
                return; // connection 在此析构
            }

            m_connections.push(std::move(connection));
        }
        m_condition.notify_one();
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_beStop = true;
        }
        m_condition.notify_all();
    }

private:
    Factory m_factory;
    std::size_t m_maxPoolSize;
    std::size_t m_minPoolSize;
    std::size_t m_currentSize; // 总连接数（空闲 + 借出）
    std::chrono::milliseconds m_deadLineMs;
    bool m_beStop = false;
    std::queue<std::unique_ptr<T> > m_connections;
    std::condition_variable m_condition;
    std::mutex m_mutex;
};

#endif // GATESERVER_CONNECTIONPOOL_H
