#include "RPCConnectPool.h"

#include <chrono>
#include <utility>


RPCConnectPool::RPCConnectPool(const std::size_t pool_size, std::string host, std::string port)
    : m_beStop(false),
      m_poolSize(pool_size),
      m_host(std::move(host)),
      m_port(std::move(port))
{
    for (std::size_t i = 0; i < m_poolSize; ++i)
    {
        const std::shared_ptr<Channel> channel = grpc::CreateChannel(
            std::format("{}:{}", m_host, m_port),
            grpc::InsecureChannelCredentials());
        m_connections.push(VerifyService::NewStub(channel));
    }
}

RPCConnectPool::~RPCConnectPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    close();
    while (!m_connections.empty())
    {
        m_connections.pop();
    }
}

void RPCConnectPool::close()
{
    m_beStop = true;
    m_condition.notify_all(); // 通知所有线程
}

std::unique_ptr<VerifyService::Stub> RPCConnectPool::getConnection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // 限时等待（与 MySQL/Redis 池策略一致）：池为空时不允许永久阻塞调用线程，超时返回空由调用方判空
    if (!m_condition.wait_for(lock, std::chrono::seconds(2), [this]()
        {
            if (m_beStop)
            {
                return true;
            }
            return !m_connections.empty();
        }))
    {
        return nullptr;
    }

    if (m_beStop)
    {
        return nullptr;
    }

    auto context = std::move(m_connections.front());
    m_connections.pop();
    return context;
}

void RPCConnectPool::returnConnection(std::unique_ptr<VerifyService::Stub> context)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_beStop)
    {
        return;
    }

    m_connections.push(std::move(context));
    m_condition.notify_one(); // 换一个连接通知一个线程
}
