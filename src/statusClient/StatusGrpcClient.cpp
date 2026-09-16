#include "StatusGrpcClient.h"
#include "../ConfigManager.h"

GetChatServerRsp StatusGrpcClient::getChatServer(const int uid) const
{
    ClientContext context;
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
    auto stub = m_pool->getConnection();
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
    m_pool.reset(new StatusConnectPool(
        5,
        config["StatusServer"]["host"],
        config["StatusServer"]["port"]
        ));
}
