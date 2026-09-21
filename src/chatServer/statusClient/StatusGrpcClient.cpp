#include "StatusGrpcClient.h"

#include <memory>
#include <ConfigManager.h>

namespace chat
{
message::LoginRsp StatusGrpcClient::login(const size_t uid, const std::string& token) const
{
    ClientContext context;
    message::LoginReq request;
    message::LoginRsp reply;
    request.set_token(token);
    request.set_uid(uid);
    auto stub = m_pool->getConnection();
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
    m_pool = std::make_unique<detil::StatusConnectPool>(
        5,
        config["StatusServerForClient"]["host"],
        config["StatusServerForClient"]["port"]
        );
}
}
