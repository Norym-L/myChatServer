#ifndef GATESERVER_CHATSERVICEIMPL_H
#define GATESERVER_CHATSERVICEIMPL_H

#include <message.grpc.pb.h>

#include "const.h"
#include "data/UserInfo.h"

using grpc::Channel;
using grpc::Status;
using grpc::ServerContext;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::ChatService;

using message::NotifyChatMessageReq;
using message::NotifyChatMessageRsp;

class ChatServiceImpl final : public ChatService::Service
{
public:
    ChatServiceImpl();
    [[nodiscard]] Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response) override;
    [[nodiscard]] Status ReplyAddFriend(ServerContext* context, const message::ReplyFriendReq* request, message::ReplyFriendRsp* response) override;
    [[nodiscard]] Status NotifyChatMessage(ServerContext* context, const NotifyChatMessageReq* request, NotifyChatMessageRsp* response) override;
    [[nodiscard]] std::shared_ptr<UserInfo> getBaseInfo(const std::string& base_key, std::uint64_t uid);
};


#endif //GATESERVER_CHATSERVICEIMPL_H
