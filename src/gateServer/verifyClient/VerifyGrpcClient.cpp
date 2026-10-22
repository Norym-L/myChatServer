#include "VerifyGrpcClient.h"

#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <ConfigManager.h>

GetVerifyRsp VerifyGrpcClient::getverifyCode(const std::string& email) const
{
    ClientContext context;
    // 必须设置超时：对端假死（连接建立但不响应）时，同步 RPC 会永久阻塞调用线程
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    GetVerifyRsp reply;
    GetVerifyReq request;
    request.set_email(email);

    auto stub = m_pool->getConnection();
    // 池关闭时会返回空连接，必须判空后再调用 stub
    if (stub == nullptr)
    {
        SPDLOG_ERROR("[VerifyGrpcClient] GetVerifyCode Error: no available connection");
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
    const Status status = stub->GetVerifyCode(&context, request, &reply);
    if (status.ok())
    {
        m_pool->returnConnection(std::move(stub));
        return reply;
    }
    else
    {
        m_pool->returnConnection(std::move(stub));
        SPDLOG_ERROR("[VerifyGrpcClient] GetVerifyCode Error error_code: {}", status.error_code());
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
}

VerifyGrpcClient::VerifyGrpcClient()
{
    auto& config = ConfigManager::GetInstance();
    auto host = config["VerifyServer"]["host"];
    auto port = config["VerifyServer"]["port"];
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        std::format("{}:{}", host, port),
        grpc::InsecureChannelCredentials());

    m_pool = std::make_unique<ConnectionPool<VerifyService::Stub> >(
        [channel]()
            {
                return VerifyService::NewStub(channel);
            },
        2, 4, std::chrono::milliseconds(2000)
        );
}
