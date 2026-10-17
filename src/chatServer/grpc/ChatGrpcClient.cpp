#include "ChatGrpcClient.h"

#include <grpcpp/create_channel.h>

#include "ConfigManager.h"
#include "const.h"

AddFriendRsp ChatGrpcClient::addFriend(const std::string& server_ip, const AddFriendReq& req)
{
    ClientContext context;
    // 必须设置超时：对端假死（连接建立但不响应）时，同步 RPC 会永久阻塞调用线程
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    AddFriendRsp reply;

    auto stub = m_pool[server_ip]->getConnection();
    // 池关闭时会返回空连接，必须判空后再调用 stub
    if (stub == nullptr)
    {
        reply.set_error(ErrorCode::RPC_FAILED);
        return reply;
    }
    const Status status = stub->NotifyAddFriend(&context, req, &reply);
    if (status.ok())
    {
        m_pool[server_ip]->returnConnection(std::move(stub));
        return reply;
    }
    else
    {
        m_pool[server_ip]->returnConnection(std::move(stub));
        reply.set_error(ErrorCode::RPC_FAILED);
        return reply;
    }
}

ReplyFriendRsp ChatGrpcClient::replyFriend(const std::string& server_ip, const ReplyFriendReq& req)
{
    ClientContext context;
    // 必须设置超时：对端假死（连接建立但不响应）时，同步 RPC 会永久阻塞调用线程
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    ReplyFriendRsp reply;

    auto stub = m_pool[server_ip]->getConnection();
    // 池关闭时会返回空连接，必须判空后再调用 stub
    if (stub == nullptr)
    {
        reply.set_error(ErrorCode::RPC_FAILED);
        return reply;
    }
    const Status status = stub->ReplyAddFriend(&context, req, &reply);
    if (status.ok())
    {
        m_pool[server_ip]->returnConnection(std::move(stub));
        return reply;
    }
    else
    {
        m_pool[server_ip]->returnConnection(std::move(stub));
        reply.set_error(ErrorCode::RPC_FAILED);
        return reply;
    }
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
    const auto server_list = config["PeerServer"]["servers"];

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
            config[chat_server]["host"] + ":" + config[chat_server]["grpcPort"],
            grpc::InsecureChannelCredentials());

        m_pool.emplace(
            config[chat_server]["name"],
            std::make_unique<ConnectionPool<ChatService::Stub> >(
                [channel]() { return ChatService::NewStub(channel); },
                5, 8, std::chrono::milliseconds(3000)
                ));
    }
}
