#ifndef GATESERVER_CHATGRPCCLIENT_H
#define GATESERVER_CHATGRPCCLIENT_H

#include <message.grpc.pb.h>
#include <string>
#include <json/value.h>

#include "ConnectionPool.h"
#include "Singleton.h"
#include "data/UserInfo.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::ReplyFriendReq;
using message::ReplyFriendRsp;

using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::ChatService;

using message::NotifyChatMessageRsp;
using message::NotifyChatMessageReq;

class ChatGrpcClient final : public TSingLeton<ChatGrpcClient>
{
    friend class TSingLeton;

public:
    [[nodiscard]] AddFriendRsp addFriend(const std::string& server_ip, const AddFriendReq& req);
    [[nodiscard]] ReplyFriendRsp replyFriend(const std::string& server_ip, const ReplyFriendReq& req);
    [[nodiscard]] NotifyChatMessageRsp notifyChatMessage(const std::string& server_ip, const NotifyChatMessageReq& req);
    [[nodiscard]] std::shared_ptr<UserInfo> getBaseInfo(const std::string& base_key, std::uint64_t uid);

private:
    ChatGrpcClient();

    using ChatPeerPool = std::unique_ptr<ConnectionPool<ChatService::Stub> >;
    std::unordered_map<std::string, ChatPeerPool> m_pool;
};


#endif //GATESERVER_CHATGRPCCLIENT_H
