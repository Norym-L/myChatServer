#include "ChatServiceImpl.h"

#include "../data/UserManager.h"
#include "../net/CSession.h"

ChatServiceImpl::ChatServiceImpl() = default;

Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response)
{
    const auto session = UserManager::GetInstance().getSession(request->touid());
    if (!session)
    {
        response->set_error(ErrorCode::USER_UID_INVALID);
        return Status::OK;
    }
    session->sendAuthFriendRequest(request->formuid(), request->name(), request->desc(), request->icon());
    response->set_error(ErrorCode::SUCCESS);
    spdlog::debug("ChatServiceImpl::NotifyAddFriend send add friend request to: {} form: {}", request->touid(), request->formuid());
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
