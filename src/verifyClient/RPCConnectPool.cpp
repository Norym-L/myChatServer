#include "RPCConnectPool.h"

#include <utility>


RPCConnectPool::RPCConnectPool(const std::size_t pool_size, std::string host, std::string prot)
    : m_beStop(false),
      m_poolSize(pool_size),
      m_host(std::move(host)),
      m_prot(std::move(prot))
{
    for (std::size_t i = 0; i < m_poolSize; ++i)
    {
        const std::shared_ptr<Channel> channel = grpc::CreateChannel(
        std::format("{}:{}", m_host, m_prot),
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
    // 如果m_connections.empty()，会把线程挂起并释放锁，等待 notify_all，notify_one 来唤醒并继续加锁
    m_condition.wait(lock, [this]()
    {
        if (m_beStop)
        {
            return true;
        }
        return !m_connections.empty();
    });

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
