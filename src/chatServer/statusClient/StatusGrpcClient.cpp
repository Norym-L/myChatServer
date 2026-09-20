#include "StatusGrpcClient.h"

#include <memory>
#include <ConfigManager.h>

namespace chat
{
message::LoginRsp StatusGrpcClient::login(size_t uid, const std::string& token)
{
    // TODO
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
