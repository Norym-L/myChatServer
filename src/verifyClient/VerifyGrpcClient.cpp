#include "VerifyGrpcClient.h"

#include <memory>
#include "../ConfigManager.h"

GetVerifyRsp VerifyGrpcClient::getverifyCode(const std::string &email) const
{
    ClientContext context;
    GetVerifyRsp reply;
    GetVerifyReq request;
    request.set_email(email);

    auto stub = m_pool->getConnection();
    const Status status = stub->GetVerifyCode(&context, request, &reply);
    if (status.ok())
    {
        m_pool->returnConnection(std::move(stub));
        return reply;
    }
    else
    {
        m_pool->returnConnection(std::move(stub));
        std::cerr << "GetVerifyCode Error error_code: " << status.error_code() << std::endl;
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
}

VerifyGrpcClient::VerifyGrpcClient()
{
    auto& config = ConfigManager::GetInstance();
    auto host = config["VerifyServer"]["host"];
    auto port = config["VerifyServer"]["port"];
    m_pool = std::make_unique<RPCConnectPool>(5, host, port);
}
