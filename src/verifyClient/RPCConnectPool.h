#ifndef GATESERVER_RPCCONNECTPOOL_H
#define GATESERVER_RPCCONNECTPOOL_H

#include <mutex>
#include <condition_variable>
#include <grpcpp/grpcpp.h>
#include <message.grpc.pb.h>

#include "../const.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVerifyReq;
using message::GetVerifyRsp;
using message::VerifyServer;

#include <queue>
#include <atomic>
#include <cstddef>

class RPCConnectPool
{
public:
    RPCConnectPool(std::size_t pool_size, std::string  host, std::string  prot);
    ~RPCConnectPool();

    /// 通知资源需要回收
    void close();

    /// 获取连接
    std::unique_ptr<VerifyServer::Stub> getConnection();

    /// 回收连接
    void returnConnection(std::unique_ptr<VerifyServer::Stub> context);

private:
    std::atomic<bool> m_beStop;
    std::size_t m_poolSize;
    std::string m_host;
    std::string m_prot;
    std::queue<std::unique_ptr<VerifyServer::Stub>> m_connections;
    std::condition_variable m_condition;
    std::mutex m_mutex; //TODO 改双锁，队列头尾各一把，提高并发性能
};


#endif //GATESERVER_RPCCONNECTPOOL_H
