#include "ChatServiceImpl.h"

ChatServiceImpl::ChatServiceImpl() = default;

Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response)
{
    return Status::OK;
}

Status ChatServiceImpl::NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* response)
{
    return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMessage(ServerContext* context, const TextChatMessageReq* request, TextChatMessageRsp* response)
{
    return Status::OK;
}

std::shared_ptr<UserInfo> ChatServiceImpl::getBaseInfo(const std::string& base_key, std::uint64_t uid)
{
    return nullptr;
}
