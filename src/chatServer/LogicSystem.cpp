#include "LogicSystem.h"

#include <charconv>
#include <optional>
#include <utility>
#include <vector>

#include <boost/asio/detached.hpp>

#include <json/json.h>
#include <json/reader.h>

#include <Defer.h>
#include <MessageType.h>
#include <sql/mysql/MySqlManager.h>
#include <utils/UidCodec.h>

#include "utils/config.h"
#include "utils/ServerRegistry.h"
#include "RedisManager.h"
#include "data/UserManager.h"
#include "grpc/ChatGrpcClient.h"
#include "net/CSession.h"
#include "statusClient/StatusGrpcClient.h"

LogicSystem::~LogicSystem()
{
    // 关闭 channel 让消费协程退出，再停止 io_context 并回收逻辑线程
    m_messageChannel.close();
    m_workGuard.reset();
    m_ioContext.stop();
    for (auto& t: m_ioTreads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

LogicSystem::LogicSystem()
    : m_workGuard(net::make_work_guard(m_ioContext)),
      m_messageChannel(m_ioContext, 1000)
{
    m_selfServerName = ResolveSelfSection(ConfigManager::GetInstance());
    registerCallBacks();
    for (auto i = 0; i < 100; ++i) // 200 并发
    {
        net::co_spawn(m_ioContext, dealMessage(),
                      [](const std::exception_ptr& ep)
                          {
                              if (ep)
                              {
                                  SPDLOG_ERROR("[logic system] message loop exited with error: {}", Exception_ptr_to_string(ep));
                              }
                          });
    }
    for (int i = 0; i < 4; ++i)
    {
        m_ioTreads.emplace_back([this]()
            {
                m_ioContext.run();
            });
    }
}


void LogicSystem::postMessageToQueue(const std::shared_ptr<LogicNode>& logic_node)
{
    // channel 是线程安全的：连接线程只负责投递，真正的处理在逻辑线程里串行执行
    boost::system::error_code ec;
    if (!m_messageChannel.try_send(ec, logic_node))
    {
        SPDLOG_WARN("[logic system] the message queue is full, drop message id: {}, error: {}",
                    logic_node->receviceNode->messageId, ec.message());
    }
}

void LogicSystem::offlineUser(const std::uint64_t uid, const std::string& session_id)
{
    net::co_spawn(m_ioContext, RemoveOnlineKey(uid, session_id, m_selfServerName), net::detached);
}

net::awaitable<void> LogicSystem::RemoveOnlineKey(const std::uint64_t uid, std::string session_id, std::string server_name)
{
    try
    {
        // 用户可能还没等旧连接被服务端发现断开，就用新连接重新登录了：
        // 此时 uip_<uid> 已由新会话接管，旧连接不能把新会话的在线状态删掉。
        const auto current = UserManager::GetInstance().getSession(uid);
        if (current && current->getSessionId() != session_id)
        {
            SPDLOG_INFO("[logic] skip removing online key: uid {} now on session {}, closing session {}",
                        uid, current->getSessionId(), session_id);
            co_return;
        }

        // 跨实例：新会话可能登录到了别的 ChatServer，此时 uip_<uid> 已指向那台机器。
        const std::string key = USER_IP_PERFIX + std::to_string(uid);
        const auto online_server = co_await RedisManager::GetInstance().get(key);
        if (online_server.has_value() && !online_server->empty() && *online_server != server_name)
        {
            SPDLOG_INFO("[logic] skip removing online key: uid {} is now on {}, closing on {}",
                        uid, *online_server, server_name);
            co_return;
        }
        co_await RedisManager::GetInstance().del({key});
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic system] remove online key failed, uid: {}, error: {}", uid, e.what());
    }
}

net::awaitable<std::shared_ptr<UserInfo> > LogicSystem::GetBaseInfo(const std::uint64_t uid)
{
    // 先查redis
    try
    {
        if (const auto info_str = co_await RedisManager::GetInstance().getUserInfo(uid); info_str.has_value())
        {
            co_return std::make_shared<UserInfo>(info_str.value());
        }
    }
    catch (const std::exception& e)
    {
        // Redis 不可用时不能挡住登录：继续回落数据库
        SPDLOG_WARN("[logic] get base info from redis failed, error: {}", e.what());
    }

    // 数据库查询
    auto [error_code, value] = co_await MySqlManager::GetInstance().co_run().getUser(uid);
    if (error_code != 0 || !value.has_value())
    {
        co_return nullptr;
    }
    const auto& info = value.value();

    try
    {
        co_await RedisManager::GetInstance().setUserBaseInfo(uid, info);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic][LogicSystem::getBaseInfo] Redis insert error: {}", e.what());
    }
    co_return std::make_shared<UserInfo>(info);
}

net::awaitable<void> LogicSystem::dealMessage()
{
    for (;;)
    {
        std::shared_ptr<LogicNode> message_node;
        try
        {
            message_node = co_await m_messageChannel.async_receive(net::use_awaitable);
        }
        catch (const boost::system::system_error& e)
        {
            // channel 已关闭（进程退出），消费协程结束
            SPDLOG_DEBUG("[logic system] dealMessage() stop: {}", e.what());
            co_return;
        }

        const auto message_id = message_node->receviceNode->messageId;
        const auto call_back_function = m_callbackFunctions.find(message_id);
        if (call_back_function == m_callbackFunctions.end())
        {
            SPDLOG_WARN("[logic system] dealMessage() unknow message id: {}", message_id);
            continue;
        }

        // 必须用具名变量持有报文：协程参数是引用，临时对象会在 co_await 挂起期间被销毁
        const std::string message_data(message_node->receviceNode->data,
                                       message_node->receviceNode->currentLen);

        // 串行处理：上一个请求（含其中的 Redis 往返）结束才处理下一个，与旧逻辑线程模型一致
        try
        {
            co_await call_back_function->second(message_node->session, message_data);
        }
        catch (const std::exception& e)
        {
            // 单个请求失败不能拖垮整个消费循环
            SPDLOG_ERROR("[logic system] dealMessage() handle message id: {} failed, error: {}", message_id, e.what());
        }
        SPDLOG_INFO("[logic system] dealMessage() handle message id: {}", message_id);
    }
}


void LogicSystem::registerCallBacks()
{
    m_callbackFunctions[MessageType::MESSAGE_CHAT_LOGIN_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await loginHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SEARCH_USER_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await searchUserHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_ADD_FRIEND_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await sendFriendRequestHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_FRIEND_REQUEST_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await syncFriendRequestListHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_HEART_BEAT_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await heartbeatHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_CHECK_IS_USER_ONLINE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await checkIsUserOnlineHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_AUTH_FRIEND_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await authFriendRequestHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_FRIEND_LIST_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await syncUserFriendList(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_TEXT_CHAT_MSG_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await sendTextMessageHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_CHAT_MESSAGE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await syncChatMessageHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_ACK_CHAT_MESSAGE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await ackChatMessageHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_DEL_FRIEDN_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await delFriendHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SET_FRIEND_REMARK_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await setFriendRemarkhandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_CHANGE_USER_INFO_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await changeUserInfoHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_GROUP_CREATE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await createChatGroupHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_GROUP_ADD_MEMBERS_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await chatGroupAddMembersHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_GROUP_KICK_MEMBER_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await chatGroupKickMemberHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_GROUP_DISMISS_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await chatGroupDismissHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_GET_GROUP_USER_INFO_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data) -> net::awaitable<void>
            {
                co_await chatGroupGetUserInfosHandler(session, message_data);
            };
}

net::awaitable<void> LogicSystem::loginHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX_WITHOUT_CHECK_LOGIN(MESSAGE_CHAT_LOGIN_RSP)

    // 客户端 uid 统一为字符串：非字符串或非纯数字一律按非法 uid 处理，避免 jsoncpp 抛异常
    const Json::Value& uid_value = root["uid"];
    const auto token = root["token"].asString();

    std::uint64_t uid{0};
    if (!uid_value.isString() || !uid_codec::ParseUid(uid_value.asString(), uid))
    {
        SPDLOG_DEBUG("[logic system] login with invalid uid: {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    SPDLOG_DEBUG("[logic system] user login, id: {}, token: {}", uid, token);

    const std::string uid_str = std::to_string(uid);
    auto& redis = RedisManager::GetInstance();

    // 从 redis 中获取用户token
    std::optional<std::string> token_value;
    try
    {
        token_value = co_await redis.get(USER_TOKEN_PERFIX + uid_str);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic system] get token from redis failed, uid: {}, error: {}", uid, e.what());
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }
    if (!token_value)
    {
        // 该 uid 在 Redis 里没有 token：要么 StatusServer 写入失败，要么客户端回传的 uid 与网关下发的不一致
        SPDLOG_WARN("[logic system] token not found in redis, key: {}, client uid: {}",
                    USER_TOKEN_PERFIX + uid_str, uid);
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        co_return;
    }
    if (*token_value != token)
    {
        return_value["error"] = ErrorCode::USER_TOKEN_INVALID;
        co_return;
    }

    if (!co_await redis.registUser(uid, m_selfServerName)) // redis注册失败
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }

    // 加载好友列表到redis
    bool friend_set_exists = false;
    try
    {
        friend_set_exists = co_await redis.exists(USER_FRIEND_SET + uid_str);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic system] check friend set failed, uid: {}, error: {}", uid, e.what());
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }
    if (!friend_set_exists)
    {
        std::vector<std::uint64_t> friend_uids;
        friend_uids.emplace_back(0);
        // 加入一个空值，防止用户在线时好友被删为0使redis set被删除，再次添加好友时会因为set不存在而产生"你是对方的好友而对方不是你的好友"的bug
        auto [error_code, friend_infos] = co_await MySqlManager::GetInstance().co_run().getUserFriendList(uid);
        for (const auto& [remark, friend_uid]: friend_infos)
        {
            friend_uids.push_back(friend_uid);
        }
        if (!co_await redis.loadFriends(uid, std::move(friend_uids)))
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
            co_return;
        }
    }

    auto user_info = co_await GetBaseInfo(uid);
    if (!user_info)
    {
        // Redis 缓存未命中且 MySQL 也查不到该 uid（MySqlDao 查询异常时同样返回空）
        SPDLOG_WARN("[logic system] base info not found, uid: {}", uid);
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        co_return;
    }

    return_value["uid"] = uid_str; // uid 对外统一为字符串
    return_value["token"] = *token_value;
    return_value["name"] = user_info->name;
    return_value["email"] = user_info->email;
    return_value["desc"] = user_info->desc;
    return_value["sex"] = user_info->sex;
    return_value["icon"] = user_info->icon;

    // 本实例连接数 +1：由 ServerRegistry 统一维护并写入 Redis（logincount_<name>，随心跳续期）。
    // 同一 session 重复登录不重复计数，避免断开时只减一次导致计数只增不减
    if (session->getUserId() == 0)
    {
        ServerRegistry::GetInstance().addConnection();
    }

    // 绑定用户session
    session->setUserId(uid);

    // 绑定uid和session，实现踢人操作
    UserManager::GetInstance().setUserSession(uid, session);

    // PEL 中是否有"已投递未 ACK"的消息；提示客户端登录后立即拉取
    return_value["has_pending"] = (co_await redis.getPendingMessageCount(uid)) > 0 ? "1" : "0";
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::heartbeatHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_HEART_BEAT_RSP)

    const std::uint64_t uid = session->getUserId();

    // 续期在线状态 key：30 秒内没有再收到心跳，key 自动过期，isUserOnLine 即判定用户离线
    const auto ip_key = USER_IP_PERFIX + std::to_string(uid);
    try
    {
        co_await RedisManager::GetInstance().set(ip_key, m_selfServerName, HEART_BEAT_EXPIRE_SECONDS);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic][LogicSystem::heartbeatHandler] Redis insert error, uid: {}, error: {}", uid, e.what());
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }

    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SEARCH_USER_RSP)

    // 客户端 uid 用字符串传递；非字符串按空串处理，避免 jsoncpp 抛异常终止逻辑线程
    const Json::Value& search_value = root["uid"];
    const auto user_name_or_uid = search_value.isString() ? search_value.asString() : std::string(); // 可能是 uid 也有可能是 用户名

    SPDLOG_DEBUG("[logic system] user search, search value is: {}", user_name_or_uid);

    std::uint64_t uid{};
    const auto [ptr, ec] = std::from_chars(
        user_name_or_uid.c_str(), user_name_or_uid.c_str() + user_name_or_uid.size(), uid);
    // 必须同时满足无错误且全部字符被消费：空串在 from_chars 下同样满足 ptr == end，
    // 但没有 ec 判断时会以 uid=0 去查一次库
    if (ec == std::errc() && ptr == user_name_or_uid.c_str() + user_name_or_uid.size()) // 是纯数字，先找uid
    {
        auto info = co_await SearchUserByUid(uid);
        if (info) // 找到
        {
            return_value["error"] = ErrorCode::SUCCESS;
            return_value["uid"] = std::to_string(info->uid); // uid 对外统一为字符串
            return_value["user_name"] = info->name;
            return_value["desc"] = info->desc;
            return_value["sex"] = info->sex;
            return_value["icon"] = info->icon;
            co_return;
        }
    }
    // 继续找用户名
    auto infos = co_await SearchUserByName(user_name_or_uid);

    if (infos.empty())
    {
        // 都没找到
        return_value["error"] = ErrorCode::USER_NOT_FOUND;
        co_return;
    }

    return_value["error"] = ErrorCode::SUCCESS;
    for (const auto& info: infos)
    {
        Json::Value info_value;
        info_value["uid"] = std::to_string(info->uid); // uid 对外统一为字符串
        info_value["user_name"] = info->name;
        info_value["desc"] = info->desc;
        info_value["sex"] = info->sex;
        info_value["icon"] = info->icon;
        return_value["infos"].append(info_value);
    }
}

net::awaitable<void> LogicSystem::sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_ADD_FRIEND_RSP)

    std::uint64_t form_uid{};
    std::uint64_t to_uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), form_uid) ||
        !uid_codec::ParseUid(root["to_uid"].asString(), to_uid))
    {
        SPDLOG_DEBUG("[logic system] add friend request, parse uid failed, str is {} and {}", root["uid"].asString(), root["to_uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    if (form_uid != session->getUserId())
    {
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        co_return;
    }
    SPDLOG_DEBUG("[logic system] add friend request, from {} to {}", form_uid, to_uid);
    const std::string verify_info = root["verify_info"].asString();
    const std::string remark = root["remark"].asString();

    const auto [code, add_frined_request_id] = co_await MySqlManager::GetInstance().co_run().sendFriendRequest(form_uid, to_uid, verify_info, remark);
    if (code != 0) // 异常处理
    {
        if (code == -1 || code == -2) // 这里拦截用户不存在
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
            SPDLOG_WARN("[LogicSystem::sendFriendRequestHandler] recvive a unexist uid form_uid: {} to_uid: {}", form_uid, to_uid);
        }
        else if (code == -3)
        {
            return_value["error"] = ErrorCode::CAN_NOT_ADD_YOURSELF_AS_A_FRIEND;
        }
        else if (code == -4)
        {
            return_value["error"] = ErrorCode::USER_IS_YOUR_FRIEND_BEFOR;
        }
        else if (code == -5)
        {
            return_value["error"] = ErrorCode::CHACK_YOUR_RECEIVED_FRIEND_REQUEST;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["add_friend_request_id"] = std::to_string(add_frined_request_id);
    // redis查找发送用户验证消息，如果用户在线，通过grpc发送验证请求
    const auto user_ip = co_await RedisManager::GetInstance().isUserOnLine(to_uid);
    if (!user_ip || user_ip->empty())
    {
        co_return; // 对方不在线
    }
    if (*user_ip == m_selfServerName)
    {
        // 在当前服务器
        const auto session_other = UserManager::GetInstance().getSession(to_uid);
        if (!session_other)
        {
            co_return;
        }
        session_other->sendAuthFriendRequest(add_frined_request_id);
        SPDLOG_DEBUG("LogicSystem::sendFriendRequestHandler send friend request to {}, friend request id {}", to_uid, add_frined_request_id);
        co_return;
    }
    (void) GrpcSendFriendRequestToOther(add_frined_request_id, to_uid, *user_ip);
}

net::awaitable<void> LogicSystem::syncFriendRequestListHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_FRIEND_REQUEST_RSP)

    std::uint64_t uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), uid))
    {
        SPDLOG_DEBUG("[logic system] sync friend request list, parse uid failed, str is {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    if (uid != session->getUserId())
    {
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        co_return;
    }
    SPDLOG_DEBUG("[logic system] sync friend request list, uid: {}", uid);

    const auto [error_code, mysql_value] = co_await MySqlManager::GetInstance().co_run().getFriendRequestList(uid);

    if (error_code != 0)
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }

    return_value["error"] = ErrorCode::SUCCESS;
    return_value["count"] = std::to_string(mysql_value.size());

    for (const auto& info: mysql_value)
    {
        Json::Value value;
        value["id"] = std::to_string(info.add_friend_request_id);
        value["from_uid"] = std::to_string(info.from_uid);
        value["to_uid"] = std::to_string(info.to_uid);
        value["name"] = info.name;
        value["verify_info"] = info.desc;
        value["icon"] = info.icon;
        value["sex"] = info.sex;
        value["state"] = info.status;
        value["nick"] = info.nick; // 没有备注或是接收方则 nick备注 为空字符串
        return_value["apply_infos"].append(value);
    }
}

net::awaitable<void> LogicSystem::checkIsUserOnlineHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_CHECK_IS_USER_ONLINE_RSP)

    std::uint64_t uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), uid))
    {
        SPDLOG_DEBUG("[logic system] check user online request, parse uid failed, str is {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    SPDLOG_DEBUG("[logic system] check user online request, check the uid {}", uid);

    const auto ip_key = USER_IP_PERFIX + std::to_string(uid);
    const auto isOnline = co_await RedisManager::GetInstance().get(ip_key);
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["online"] = isOnline ? "online" : "offline";
}

net::awaitable<void> LogicSystem::authFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_AUTH_FRIEND_RSP)

    return_value["add_friend_request_id"] = root["add_friend_request_id"].asString();
    std::uint64_t add_friend_request_id{};
    if (!uid_codec::ParseUid(root["add_friend_request_id"].asString(), add_friend_request_id))
    {
        SPDLOG_DEBUG("[logic system] authFriendRequestHandler, parse add_friend_request_id failed, str is {}", root["add_friend_request_id"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    int state{};
    if (!uid_codec::ParseNumber<int>(root["state"].asString(), state) ||
        state != 1 && state != 2) // 只能是接受(1)或拒绝(2)
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    SPDLOG_DEBUG("[logic system] authFriendRequestHandler, id: {} state: {}", add_friend_request_id, state);
    const auto [code, sender_uid] = co_await MySqlManager::GetInstance().co_run().handleFriendRequest(add_friend_request_id, session->getUserId(), static_cast<FriendApplyStatus>(state));
    if (code != 0)
    {
        if (code == -1)
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
        }
        else if (code == -2)
        {
            return_value["error"] = ErrorCode::DOUBLE_HANDLE_REQUEST;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }

    if (state == 1 && !co_await RedisManager::GetInstance().addFriend(session->getUserId(), sender_uid))
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }
    return_value["error"] = ErrorCode::SUCCESS;


    // redis查找发送用户验证消息，如果用户在线，通过grpc发送验证请求
    const auto user_ip = co_await RedisManager::GetInstance().isUserOnLine(sender_uid);
    if (!user_ip || user_ip->empty())
    {
        co_return; // 对方不在线
    }
    if (*user_ip == m_selfServerName)
    {
        // 在当前服务器
        const auto session_other = UserManager::GetInstance().getSession(sender_uid);
        if (!session_other)
        {
            co_return;
        }
        session_other->sendFriendRequestResult(add_friend_request_id, state);
        SPDLOG_DEBUG("LogicSystem::authFriendRequestHandler send auth friend request to {}, status {}", sender_uid, state);
        co_return;
    }

    (void) GrpcAuthFriendRequestAndNotifyToOther(sender_uid, add_friend_request_id, state, *user_ip);
}

net::awaitable<void> LogicSystem::sendTextMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    //  -> redis的离线序列 -> 查询是否在线 -> 发送到指定用户 -> 接收用户回包显示接收到 -> 删除redis等待序列数据
    HANDLER_PREFIX(MESSAGE_TEXT_CHAT_MSG_RSP)

    return_value["msg_id"] = root["msg_id"].asString(); // 客户端用于识别消息的id 字符串传递

    std::uint64_t to_uid{};
    int message_type{};
    if (!uid_codec::ParseUid(root["to_uid"].asString(), to_uid) ||
        !uid_codec::ParseNumber(root["type"].asString(), message_type) ||
        message_type > 4 || message_type < 1)
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    bool is_group = root["group"].asBool();
    if (is_group)
    {
        if (!co_await RedisManager::GetInstance().isInGroup(to_uid, session->getUserId()))
        {
            return_value["error"] = ErrorCode::NOT_FRIEND;
            co_return;
        }
    }
    else
    {
        if (!co_await RedisManager::GetInstance().isFriend(session->getUserId(), to_uid))
        {
            return_value["error"] = ErrorCode::NOT_FRIEND;
            co_return;
        }
    }

    co_await RedisManager::GetInstance().insertMessageToOffline({
        .type        = static_cast<chat_message::Type>(message_type),
        .from_uid    = session->getUserId(),
        .to_uid      = to_uid,
        .is_to_group = is_group,
        .body        = root["body"].asString(),
        .message_id  = {} // 由 redis 生成
    });

    return_value["error"] = ErrorCode::SUCCESS;

    co_await notifyMessageSend(is_group, to_uid);
}

net::awaitable<void> LogicSystem::syncUserFriendList(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_FRIEND_LIST_RSP)

    const auto [_, friend_list_only_uid_and_remark] = co_await MySqlManager::GetInstance().co_run().getUserFriendList(session->getUserId());

    for (const auto& [remark, uid]: friend_list_only_uid_and_remark)
    {
        const auto friend_info = co_await SearchUserByUid(uid);
        if (!friend_info) continue;
        Json::Value json_info;
        json_info["uid"] = std::to_string(uid);
        json_info["name"] = friend_info->name;
        json_info["desc"] = friend_info->desc;
        json_info["nick"] = remark;
        json_info["sex"] = friend_info->sex;
        json_info["icon"] = friend_info->icon;
        return_value["friends"].append(json_info);
    }

    return_value["error"] = ErrorCode::SUCCESS;
    return_value["count"] = std::to_string(return_value["friends"].size());
}

net::awaitable<void> LogicSystem::syncChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_CHAT_MESSAGE_RSP)

    std::uint64_t count{};
    if (!uid_codec::ParseNumber(root["count"].asString(), count) || count > 10) // 不能一次拉取超过10条
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    auto& redis = RedisManager::GetInstance();
    const auto uid = session->getUserId();

    // PEL 优先：先重投"已投递未 ACK"的消息；PEL 为空时才拉取新消息。
    // 同一批 PEL 会被反复返回，客户端必须 ACK 后再拉下一批。
    auto messages = co_await redis.getMessageFromOffline(uid, static_cast<std::int64_t>(count), MessageReadMode::Pending);
    if (messages.empty())
    {
        messages = co_await redis.getMessageFromOffline(uid, static_cast<std::int64_t>(count), MessageReadMode::New);
    }
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["count"] = std::to_string(messages.size());
    for (const auto& message: messages)
    {
        Json::Value value;
        value["group"] = message.is_to_group;
        if (message.is_to_group && message.to_uid != 0) // 升级前写入的群消息没有 group_id
        {
            value["group_id"] = std::to_string(message.to_uid);
        }
        value["type"] = message.type;
        value["from_uid"] = std::to_string(message.from_uid);
        value["body"] = message.body;
        value["id"] = message.message_id; // redis 自动生成的唯一消息id（毫秒时间戳-序列号）
        return_value["msgs"].append(value);
    }
}

net::awaitable<void> LogicSystem::ackChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_ACK_CHAT_MESSAGE_RSP)

    if (!root["ids"].isArray() || root["ids"].size() > 10) // 不能一次确认10条以上
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    std::vector<std::string> message_ids;
    for (const auto& id: root["ids"])
    {
        message_ids.push_back(id.asString());
    }

    co_await RedisManager::GetInstance().ackAndDelChatMessages(session->getUserId(), message_ids);
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::delFriendHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_DEL_FRIEDN_RSP)

    return_value["uid"] = root["uid"].asString(); // 用于客户端标识请求编号
    std::uint64_t friend_uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), friend_uid))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    const auto code = co_await MySqlManager::GetInstance().co_run().deleteFriend(session->getUserId(), friend_uid);
    if (code != 0)
    {
        if (code == -1)
        {
            return_value["error"] = ErrorCode::NOT_FRIEND;
            co_return;
        }
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }
    if (!co_await RedisManager::GetInstance().removeFriend(session->getUserId(), friend_uid))
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }

    return_value["error"] = ErrorCode::SUCCESS; // 不需要通知被删除的好友
}

net::awaitable<void> LogicSystem::setFriendRemarkhandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SET_FRIEND_REMARK_RSP)

    std::uint64_t friend_uid{};
    if (!uid_codec::ParseUid(root["friend_uid"].asString(), friend_uid))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    const auto error_code = co_await MySqlManager::GetInstance().co_run().updateFriendRemark(session->getUserId(), friend_uid, root["remark"].asString());
    if (error_code != 0)
    {
        if (error_code == -1 || error_code == -2)
        {
            return_value["error"] = ErrorCode::NOT_FRIEND;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::changeUserInfoHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_CHANGE_USER_INFO_RSP)

    std::optional<std::string> name = std::nullopt;
    std::optional<std::string> desc = std::nullopt;
    std::optional<std::string> icon = std::nullopt;
    std::optional<Sex> sex = std::nullopt;

    if (!root["name"].isNull()) name = root["name"].asString();
    if (!root["desc"].isNull()) desc = root["desc"].asString();
    if (!root["icon"].isNull()) icon = root["icon"].asString();
    if (!root["sex"].isNull()) sex = static_cast<Sex>(root["sex"].asInt());

    if (!co_await RedisManager::GetInstance().changeUserInfo(session->getUserId(), name, desc, icon, sex))
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    };
    const auto error_code = co_await MySqlManager::GetInstance().co_run().changeUserInfo(session->getUserId(), name, desc, icon, sex);
    if (error_code != 0)
    {
        if (error_code == -1)
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::createChatGroupHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_GROUP_CREATE_RSP)

    // 不需要客户端发来 最大成员数量
    const auto [error_code, value] = co_await MySqlManager::GetInstance().co_run().createChatGroup(
        root["name"].asString(),
        session->getUserId(),
        root["avator"].asString(), // 群头像
        500,
        root["announcement"].asString() // 群公告
        );

    if (error_code < 0)
    {
        if (error_code == -1)
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }

    co_await RedisManager::GetInstance().insertToGroup(value, {std::to_string(session->getUserId())});
    return_value["group_id"] = std::to_string(value);
    chat_message::Message system_group_created_msg{
        .type        = chat_message::TEXT,
        .from_uid    = 1, // 系统id
        .to_uid      = value,
        .is_to_group = true,
        .body        = "群聊 " + std::to_string(value) + " 已创建",
        .message_id  = {}
    };
    co_await RedisManager::GetInstance().insertMessageToOffline(std::move(system_group_created_msg));
    return_value["error"] = ErrorCode::SUCCESS;
    co_await notifyMessageSend(true, value);
}

net::awaitable<void> LogicSystem::chatGroupAddMembersHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_GROUP_ADD_MEMBERS_RSP)

    std::uint64_t group_id{};
    if (!root["members"].isArray() || !uid_codec::ParseUid(root["group_id"].asString(), group_id))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }
    std::vector<std::string> added_members{};
    for (const auto& member: root["members"])
    {
        std::uint64_t member_uid{};
        if (!uid_codec::ParseUid(member.asString(), member_uid))
        {
            continue;
        }
        const auto result = co_await MySqlManager::GetInstance().co_run().addGroupMember(group_id, member_uid, session->getUserId());
        if (result < 0)
        {
            if (result == -1)
            {
                return_value["error"] = ErrorCode::GROUP_ID_INVALID;
            }
            else if (result == -2)
            {
                return_value["error"] = ErrorCode::NO_POWER;
            }
            else if (result == -3)
            {
                continue;
            }
            else
            {
                return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
            }
            co_return;
        } // 不管已经在群里的用户
        added_members.emplace_back(member.asString());
    }
    chat_message::Message system_add_member_msg{
        .type        = chat_message::TEXT,
        .from_uid    = 1, // 系统id
        .to_uid      = group_id,
        .is_to_group = true,
        .body        = "邀请了 " + std::to_string(added_members.size()) + " 个好友",
        .message_id  = {}
    };
    co_await RedisManager::GetInstance().insertToGroup(group_id, std::move(added_members));
    co_await RedisManager::GetInstance().insertMessageToOffline(std::move(system_add_member_msg));
    return_value["error"] = ErrorCode::SUCCESS;
    co_await notifyMessageSend(true, group_id);
}

net::awaitable<void> LogicSystem::chatGroupKickMemberHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_GROUP_KICK_MEMBER_RSP)

    std::uint64_t group_id{};
    std::uint64_t uid{};
    if (!uid_codec::ParseUid(root["group_id"].asString(), group_id) ||
        !uid_codec::ParseUid(root["uid"].asString(), uid))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    const auto result = co_await MySqlManager::GetInstance().co_run().kickGroupMember(group_id, uid, session->getUserId());
    if (result < 0)
    {
        if (result == -1)
        {
            return_value["error"] = ErrorCode::GROUP_ID_INVALID;
        }
        else if (result == -2 ||
                 result == -4 ||
                 result == -5)
        {
            return_value["error"] = ErrorCode::NO_POWER;
        }
        else if (result == -3)
        {
            return_value["error"] = ErrorCode::USER_NOT_FOUND;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }
    chat_message::Message system_kick_member_msg{
        .type        = chat_message::TEXT,
        .from_uid    = 1, // 系统id
        .to_uid      = group_id,
        .is_to_group = true,
        .body        = "踢出了 " + root["uid"].asString(),
        .message_id  = {}
    };
    co_await RedisManager::GetInstance().insertMessageToOffline(std::move(system_kick_member_msg));
    co_await notifyMessageSend(true, group_id);
    co_await RedisManager::GetInstance().kickUserFromGroup(group_id, root["uid"].asString());
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::chatGroupDismissHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_GROUP_DISMISS_RSP)

    std::uint64_t group_id{};
    if (!uid_codec::ParseUid(root["group_id"].asString(), group_id))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    const auto result = co_await MySqlManager::GetInstance().co_run().dismissGroup(group_id, session->getUserId());
    if (result < 0)
    {
        if (result == -1)
        {
            return_value["error"] = ErrorCode::GROUP_ID_INVALID;
        }
        if (result == -2)
        {
            return_value["error"] = ErrorCode::NO_POWER;
        }
        else
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        co_return;
    }
    chat_message::Message system_group_dismiss_msg{
        .type        = chat_message::TEXT,
        .from_uid    = 1, // 系统id
        .to_uid      = group_id,
        .is_to_group = true,
        .body        = "该群聊已解散",
        .message_id  = {}
    };
    co_await RedisManager::GetInstance().insertMessageToOffline(std::move(system_group_dismiss_msg));
    co_await notifyMessageSend(true, group_id);
    co_await RedisManager::GetInstance().del({CHAT_GROUP_PERFIX + root["group_id"].asString()});
    return_value["error"] = ErrorCode::SUCCESS;
}

net::awaitable<void> LogicSystem::chatGroupGetUserInfosHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_GET_GROUP_USER_INFO_RSP)

    std::uint64_t group_id{};
    if (!uid_codec::ParseUid(root["gid"].asString(), group_id))
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        co_return;
    }

    if (!co_await RedisManager::GetInstance().isInGroup(group_id, session->getUserId()))
    {
        return_value["error"] = ErrorCode::NO_POWER;
        co_return;
    }

    const auto& [error_code, user_infos] = co_await MySqlManager::GetInstance().co_run().getGroupMemberInfos(group_id);
    if (error_code < 0)
    {
        // isInGroup 判断过群一定存在
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        co_return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
    for (const auto& [nick_g, joinedAt, uid, role]: user_infos)
    {
        Json::Value value;
        value["nick_g"] = nick_g;
        value["join_at"] = joinedAt;
        value["uid"] = std::to_string(uid);
        value["role"] = role; // 0 成员，1管理员，2群主
        return_value["mb_infos"].append(std::move(value));
    }
}

net::awaitable<void> LogicSystem::sendSystemMessage(chat_message::Message message)
{
    message.from_uid = 1; // 1 是系统uid
    co_await RedisManager::GetInstance().insertMessageToOffline(std::move(message));
}

net::awaitable<void> LogicSystem::notifyMessageSend(const bool is_group, const std::uint64_t to_id) const
{
    // 通知对方有消息
    if (!is_group)
    {
        const auto user_ip = co_await RedisManager::GetInstance().isUserOnLine(to_id);
        if (!user_ip || user_ip->empty())
        {
            co_return; // 对方不在线
        }
        if (*user_ip == m_selfServerName)
        {
            // 在当前服务器
            const auto session_other = UserManager::GetInstance().getSession(to_id);
            if (!session_other)
            {
                // 在线 key 还在（心跳在续期）却没有会话：推送无法送达，
                // 客户端也就不会发 1023 拉取 —— 典型是旧连接断开时把新会话的映射删了。
                SPDLOG_WARN("[notify] uid {} is online but session missing, drop 1022", to_id);
                co_return;
            }
            session_other->notifyReceiveMessage();
            co_return;
        }

        (void) GrpcNotifyReceiveMessageToOther(to_id, *user_ip);
    }
    else
    {
        std::vector<std::uint64_t> uids;
        for (const auto& uid_str: co_await RedisManager::GetInstance().getUsersFromGroup(to_id))
        {
            std::uint64_t uid{};
            if (!uid_codec::ParseNumber(uid_str, uid))
            {
                continue;
            }
            uids.push_back(uid);
        }
        for (const auto& uid: uids)
        {
            const auto user_ip = co_await RedisManager::GetInstance().isUserOnLine(uid);
            if (!user_ip || user_ip->empty())
            {
                continue; // 对方不在线
            }
            if (*user_ip == m_selfServerName)
            {
                // 在当前服务器
                const auto session_other = UserManager::GetInstance().getSession(uid);
                if (!session_other)
                {
                    SPDLOG_WARN("[notify] group {} member uid {} is online but session missing, drop 1022", to_id, uid);
                    continue;
                }
                session_other->notifyReceiveMessage();
                continue;
            }

            (void) GrpcNotifyReceiveMessageToOther(uid, *user_ip);
        }
    }
}

bool LogicSystem::GrpcSendFriendRequestToOther(const std::uint64_t add_friend_request_id, const std::uint64_t to_uid, const std::string& user_ip)
{
    AddFriendReq req;
    req.set_touid(to_uid);
    req.set_addfriendrequestid(add_friend_request_id);
    const auto rsp = ChatGrpcClient::GetInstance().addFriend(user_ip, req);
    if (rsp.error() != 0)
    {
        SPDLOG_WARN("LogicSystem::GrpcSendFriendRequestToOther send grpc error, error: {}", rsp.error());
        return false;
    }
    return true;
}

bool LogicSystem::GrpcAuthFriendRequestAndNotifyToOther(const std::uint64_t sender_uid, const std::uint64_t add_friend_request_id, const int state, const std::string& user_ip)
{
    ReplyFriendReq req;
    req.set_senderuid(sender_uid);
    req.set_addfriendrequestid(add_friend_request_id);
    req.set_state(state);
    const auto rsp = ChatGrpcClient::GetInstance().replyFriend(user_ip, req);
    if (rsp.error() != 0)
    {
        SPDLOG_WARN("LogicSystem::GrpcAuthFriendRequestAndNotifyToOther send grpc error, error: {}", rsp.error());
        return false;
    }
    return true;
}

bool LogicSystem::GrpcNotifyReceiveMessageToOther(const std::uint64_t to_uid, const std::string& user_ip)
{
    NotifyChatMessageReq req;
    req.set_touid(to_uid);
    const auto rsp = ChatGrpcClient::GetInstance().notifyChatMessage(user_ip, req);
    if (rsp.error() != 0)
    {
        SPDLOG_WARN("LogicSystem::GrpcNotifyReceiveMessageToOther send grpc error, error: {}", rsp.error());
        return false;
    }
    return true;
}

net::awaitable<std::shared_ptr<UserInfo> > LogicSystem::SearchUserByUid(const uint64_t uid)
{
    // redis 查询
    if (const auto user_info = co_await RedisManager::GetInstance().getUserInfo(uid); user_info.has_value())
    {
        co_return std::make_shared<UserInfo>(user_info.value());
    }
    // 查数据库
    const auto [error, user_info] = co_await MySqlManager::GetInstance().co_run().getUser(uid);

    if (error != 0 || !user_info.has_value())
    {
        co_return nullptr;
    }

    const auto shared_info = std::make_shared<UserInfo>(user_info.value());
    co_await RedisManager::GetInstance().insert({shared_info});
    co_return shared_info;
}

net::awaitable<std::vector<std::shared_ptr<UserInfo> > > LogicSystem::SearchUserByName(const std::string& name)
{
    // redis 查询
    const auto base_key = USER_BASE_INFO_BY_NAME + name;
    try
    {
        if (const auto infos_str = co_await RedisManager::GetInstance().get(base_key); infos_str.has_value())
        {
            Json::Reader reader;
            Json::Value root;
            reader.parse(*infos_str, root);
            std::vector<std::shared_ptr<UserInfo> > user_infos;
            for (const auto& i: root["uids"])
            {
                user_infos.push_back(co_await SearchUserByUid(i.asUInt64()));
            }
            co_return user_infos;
        }
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[logic system] get user base info by name from redis failed, key: {}, error: {}", base_key, e.what());
    }

    // 查数据库
    auto [error, user_infos] = co_await MySqlManager::GetInstance().co_run().getUser(name);
    std::vector<std::shared_ptr<UserInfo> > returns;
    if (!(error != 0 || user_infos.empty()))
    {
        returns.reserve(user_infos.size());
        for (const auto& info: user_infos)
        {
            returns.emplace_back(std::make_shared<UserInfo>(info));
        }
        co_await RedisManager::GetInstance().insert(returns);
    }
    co_return returns;
}
