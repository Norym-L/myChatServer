#include "StatusGrpcClient.h"

#include <chrono>
#include <memory>
#include <ConfigManager.h>

GetChatServerRsp StatusGrpcClient::getChatServer(const int uid) const
{
    ClientContext context;
    // 必须设置超时：对端假死（连接建立但不响应）时，同步 RPC 会永久阻塞调用线程
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
    auto stub = m_pool->getConnection();
    // 池关闭时会返回空连接，必须判空后再调用 stub
    if (stub == nullptr)
    {
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
    const Status status = stub->GetChatServer(&context, request, &reply);
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
    auto &config = ConfigManager::GetInstance();
    m_pool = std::make_unique<StatusConnectPool>(
        5,
        config["StatusServerForClient"]["host"],
        config["StatusServerForClient"]["port"]
        );
}
