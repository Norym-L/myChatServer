#include "StatusGrpcClient.h"

#include <chrono>
#include <memory>
#include <ConfigManager.h>

message::LoginRsp StatusGrpcClient::login(const size_t uid, const std::string& token) const
{
    ClientContext context;
    // 必须设置超时：本方法在逻辑线程持有消息队列锁期间被调用，对端假死会让整条消息处理链停摆
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    message::LoginReq request;
    message::LoginRsp reply;
    request.set_token(token);
    request.set_uid(uid);
    auto stub = m_pool->getConnection();
    // 池关闭时会返回空连接，必须判空后再调用 stub
    if (stub == nullptr)
    {
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
    const Status status = stub->Login(&context, request, &reply);
    if (status.ok())
    {
        m_pool->returnConnection(std::move(stub));
        return reply;
    }
    else
    {
        m_pool->returnConnection(std::move(stub));
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
}

StatusGrpcClient::StatusGrpcClient()
{
    auto& config = ConfigManager::GetInstance();
    m_pool = std::make_unique<detail::StatusConnectPool>(
        5,
        config["StatusServerForClient"]["host"],
        config["StatusServerForClient"]["port"]
        );
}
