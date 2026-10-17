#include "ChatServiceImpl.h"

#include "../data/UserManager.h"
#include "../net/CSession.h"

ChatServiceImpl::ChatServiceImpl() = default;

Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* response)
{
    const auto session = UserManager::GetInstance().getSession(request->touid());
    if (!session)
    {
        response->set_error(ErrorCode::USER_OUTLINE);
        return Status::OK;
    }
    session->sendAuthFriendRequest(request->addfriendrequestid());
    response->set_error(ErrorCode::SUCCESS);
    spdlog::debug("ChatServiceImpl::NotifyAddFriend send add friend request to: {} request id: {}", request->touid(), request->addfriendrequestid());
    return Status::OK;
}

Status ChatServiceImpl::ReplyAddFriend(ServerContext* context, const message::ReplyFriendReq* request, message::ReplyFriendRsp* response)
{
    const auto session = UserManager::GetInstance().getSession(request->senderuid());
    if (!session)
    {
        response->set_error(ErrorCode::USER_OUTLINE);
        return Status::OK;
    }
    session->sendFriendRequestResult(request->addfriendrequestid(), request->state());
    response->set_error(ErrorCode::SUCCESS);
    spdlog::debug("ChatServiceImpl::ReplyAddFriend send friend request reply to: {}", request->senderuid());
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
