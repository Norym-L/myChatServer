#ifndef GATESERVER_STATUSGRPCCLIENT_H
#define GATESERVER_STATUSGRPCCLIENT_H

#include <message.grpc.pb.h>
#include <queue>
#include <grpcpp/create_channel.h>

#include "../const.h"
#include "../Singleton.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;

class StatusConnectPool
{
public:
    StatusConnectPool(const std::size_t size, const std::string &host, const std::string &port)
        : m_poolSize(size), m_host(host), m_port(port)
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

class StatusGrpcClient : public TSingLeton<StatusGrpcClient>
{
    friend class TSingLeton;

public:
    ~StatusGrpcClient() = default;
    GetChatServerRsp getChatServer(int uid) const;

private:
    StatusGrpcClient();

    std::unique_ptr<StatusConnectPool> m_pool;
};


#endif //GATESERVER_STATUSGRPCCLIENT_H
