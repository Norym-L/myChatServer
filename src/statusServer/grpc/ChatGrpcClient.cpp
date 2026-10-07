#include "ChatGrpcClient.h"

#include <grpcpp/create_channel.h>

#include "ConfigManager.h"

AddFriendRsp ChatGrpcClient::addFriend(const std::string& server_ip, const AddFriendReq& req)
{
    return AddFriendRsp{};
}

AuthFriendRsp ChatGrpcClient::authFriend(const std::string& server_ip, const AuthFriendReq& req)
{
    return AuthFriendRsp{};
}

TextChatMessageRsp ChatGrpcClient::sendTextChatMessage(const std::string& server_ip, const TextChatMessageReq& req, const Json::Value& return_value)
{
    return TextChatMessageRsp{};
}

std::shared_ptr<UserInfo> ChatGrpcClient::getBaseInfo(const std::string& base_key, std::uint64_t uid)
{
    return nullptr;
}

ChatGrpcClient::ChatGrpcClient()
{
    auto& config = ConfigManager::GetInstance();
    const auto server_list = config["ChatServer"]["servers"];

    std::vector<std::string> chat_servers{};
    {
        std::stringstream stringstream(server_list);
        std::string chat_server;

        while (std::getline(stringstream, chat_server, ','))
        {
            chat_servers.push_back(chat_server);
        }
    }
    for (const auto& chat_server: chat_servers)
    {
        if (config[chat_server]["name"].empty())
        {
            continue;
        }

        auto channel = grpc::CreateChannel(
            config[chat_server]["host"] + ":" + config[chat_server]["port"],
            grpc::InsecureChannelCredentials());

        m_pool.emplace(
            config[chat_server]["name"],
            std::make_unique<ConnectionPool<ChatService::Stub> >(
                [channel]() { return ChatService::NewStub(channel); },
                5, 8, std::chrono::milliseconds(3000)
                ));
    }
}
