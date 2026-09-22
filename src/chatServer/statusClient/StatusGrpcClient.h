#ifndef GATESERVER_STATUSGRPCCLIENT_H
#define GATESERVER_STATUSGRPCCLIENT_H

#include <chrono>
#include <condition_variable>
#include <message.grpc.pb.h>
#include <queue>
#include <utility>
#include <grpcpp/create_channel.h>

#include <const.h>
#include <Singleton.h>

namespace chat
{
using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;

namespace detail
{
class StatusConnectPool
{
public:
    StatusConnectPool(const std::size_t size, std::string  host, std::string  port)
        : m_poolSize(size), m_host(std::move(host)), m_port(std::move(port))
    {
        for (std::size_t i = 0; i < m_poolSize; ++i)
        {
            std::shared_ptr<Channel> channel = grpc::CreateChannel(m_host + ":" + m_port, grpc::InsecureChannelCredentials());
            m_connections.push(StatusService::NewStub(channel));
        }
    }

    ~StatusConnectPool()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        close();
        while (!m_connections.empty())
        {
            m_connections.pop();
        }
    }

    std::unique_ptr<StatusService::Stub> getConnection()
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

    void returnConnection(std::unique_ptr<StatusService::Stub> context)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_beStop)
        {
            return;
        }
        m_connections.push(std::move(context));
        m_condition.notify_one();
    }

    void close()
    {
        m_beStop = true;
        m_condition.notify_all();
    }

private:
    std::atomic<bool> m_beStop{false};
    std::size_t m_poolSize;
    std::string m_host;
    std::string m_port;
    std::queue<std::unique_ptr<StatusService::Stub> > m_connections;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};
}

class StatusGrpcClient : public TSingLeton<StatusGrpcClient>
{
    friend class TSingLeton;

public:
    ~StatusGrpcClient() = default;

    [[nodiscard]] message::LoginRsp login(size_t uid, const std::string& token) const;

private:
    StatusGrpcClient();

    std::unique_ptr<detail::StatusConnectPool> m_pool;
};
}
#endif //GATESERVER_STATUSGRPCCLIENT_H
