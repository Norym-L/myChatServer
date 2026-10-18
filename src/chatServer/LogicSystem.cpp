#include "LogicSystem.h"

#include <cstdint>

#include <json/json.h>
#include <json/reader.h>

#include <Defer.h>
#include <sql/mysql/MySqlManager.h>
#include <utils/UidCodec.h>

#include "utils/config.h"
#include "utils/ServerRegistry.h"
#include "RedisManager.h"
#include "data/UserManager.h"
#include "grpc/ChatGrpcClient.h"
#include "net/CSession.h"
#include "statusClient/StatusGrpcClient.h"

LogicSystem::LogicSystem()
    : m_beStop(false)
{
    RedisManager::GetInstance();
    m_selfServerName = ResolveSelfSection(ConfigManager::GetInstance());
    registerCallBacks();
    m_workThread = std::thread(&LogicSystem::dealMessage, this);
}

LogicSystem::~LogicSystem()
{
    m_beStop = true;
    m_conditionVariable.notify_one();
    m_workThread.join();
}


void LogicSystem::postMessageToQueue(const std::shared_ptr<LogicNode>& logic_node)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    const size_t messageQueue_size = m_messageQueue.size();
    if (messageQueue_size > 10)
    {
        // 队列太长，不要了
        spdlog::warn("[logic system] the message queue is full");
        return;
    }
    m_messageQueue.push(logic_node);
    lock.unlock();
    // 必须无条件通知：仅在队列非空时通知会让空闲后的首条消息永久滞留（丢唤醒）
    m_conditionVariable.notify_one();
}

std::shared_ptr<UserInfo> LogicSystem::GetBaseInfo(const std::string& base_key, const std::uint64_t uid)
{
    // 先查redis
    std::string info_str{};
    if (RedisManager::GetInstance().get(base_key, info_str))
    {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        return std::make_shared<UserInfo>(
            root["token"].asString(),
            root["name"].asString(),
            root["email"].asString(),
            root["desc"].asString(),
            root["icon"].asString(),
            root["uid"].asUInt64(),
            static_cast<Sex>(root["sex"].asUInt()));
    }
    else
    {
        // 数据库查询
        auto user_info = MySqlManager::GetInstance().getUser(uid);
        if (!user_info)
        {
            return nullptr;
        }

        Json::Value root;
        root["token"] = user_info->token;
        root["name"] = user_info->name;
        root["email"] = user_info->email;
        root["desc"] = user_info->desc;
        root["icon"] = user_info->icon;
        root["uid"] = user_info->uid;
        if (!RedisManager::GetInstance().set(base_key, root.toStyledString(), 500))
        {
            spdlog::warn("[logic][LogicSystem::getBaseInfo] Redis insert error!");
        }
        return user_info;
    }
}

void LogicSystem::dealMessage()
{
    for (;;)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_messageQueue.empty() && !m_beStop)
        {
            // 等待数据
            m_conditionVariable.wait(lock);
        }

        if (m_beStop)
        {
            while (!m_messageQueue.empty())
            {
                const auto message_node = m_messageQueue.front();
                auto call_back_function = m_callbackFunctions.find(message_node->m_receviceNdoe->m_messageId);
                if (call_back_function == m_callbackFunctions.end())
                {
                    spdlog::warn("[logic system] dealMessage() unknow message id: {}", message_node->m_receviceNdoe->m_messageId);
                    m_messageQueue.pop();
                    continue;
                }
                call_back_function->second(
                    message_node->m_session,
                    std::string(message_node->m_receviceNdoe->m_data, message_node->m_receviceNdoe->m_currentLen));
                m_messageQueue.pop();
                spdlog::info("[logic system] dealMessage() handle message id: {}", message_node->m_receviceNdoe->m_messageId);
            }
            break;
        }

        const auto message_node = m_messageQueue.front();
        auto call_back_function = m_callbackFunctions.find(message_node->m_receviceNdoe->m_messageId);
        if (call_back_function == m_callbackFunctions.end())
        {
            spdlog::warn("[logic system] dealMessage() unknow message id: {}", message_node->m_receviceNdoe->m_messageId);
            m_messageQueue.pop();
            continue;
        }
        call_back_function->second(
            message_node->m_session,
            std::string(message_node->m_receviceNdoe->m_data, message_node->m_receviceNdoe->m_currentLen));
        m_messageQueue.pop();
        spdlog::info("[logic system] dealMessage() handle message id: {}", message_node->m_receviceNdoe->m_messageId);
    }
}


void LogicSystem::registerCallBacks()
{
    m_callbackFunctions[MessageType::MESSAGE_CHAT_LOGIN_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                loginHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SEARCH_USER_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                searchUserHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_ADD_FRIEND_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                sendFriendRequestHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_FRIEND_REQUEST_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                syncFriendRequestListHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_HEART_BEAT_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                heartbeatHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_CHECK_IS_USER_ONLINE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                checkIsUserOnlineHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_AUTH_FRIEND_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                authFriendRequestHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_FRIEND_LIST_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                syncUserFriendList(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_TEXT_CHAT_MSG_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                sendTextMessageHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_SYNC_CHAT_MESSAGE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                syncChatMessageHandler(session, message_data);
            };

    m_callbackFunctions[MessageType::MESSAGE_ACK_CHAT_MESSAGE_REQ] = [this](
        const std::shared_ptr<CSession>& session,
        const std::string& message_data)
            {
                ackChatMessageHandler(session, message_data);
            };
}

void LogicSystem::loginHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_CHAT_LOGIN_RSP)

    // 客户端 uid 统一为字符串：非字符串或非纯数字一律按非法 uid 处理，避免 jsoncpp 抛异常
    const Json::Value& uid_value = root["uid"];
    const auto token = root["token"].asString();

    std::uint64_t uid{0};
    if (!uid_value.isString() || !uid_codec::ParseUid(uid_value.asString(), uid))
    {
        spdlog::debug("[logic system] login with invalid uid: {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }

    spdlog::debug("[logic system] user login, id: {}, token: {}", uid, token);

    const std::string uid_str = std::to_string(uid);
    // 从 redis 中获取用户token
    std::string token_value{};
    if (!RedisManager::GetInstance().get(USER_TOKEN_PERFIX + uid_str, token_value))
    {
        // 该 uid 在 Redis 里没有 token：要么 StatusServer 写入失败，要么客户端回传的 uid 与网关下发的不一致
        spdlog::warn("[logic system] token not found in redis, key: {}, client uid: {}",
                     USER_TOKEN_PERFIX + uid_str, uid);
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }
    else if (token_value != token)
    {
        return_value["error"] = ErrorCode::USER_TOKEN_INVALID;
        return;
    }
    else if (!RedisManager::GetInstance().registUser(uid, m_selfServerName)) // redis注册失败
    {
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        return;
    }
    else
    {
        return_value["error"] = ErrorCode::SUCCESS;
    }

    std::string base_key = USER_BASE_INFO + uid_str;
    auto user_info = GetBaseInfo(base_key, uid);
    if (!user_info)
    {
        // Redis 缓存未命中且 MySQL 也查不到该 uid（MySqlDao 查询异常时同样返回空）
        spdlog::warn("[logic system] base info not found, key: {}, uid: {}", base_key, uid);
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }

    return_value["uid"] = uid_str; // uid 对外统一为字符串
    return_value["token"] = token_value;
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
}

void LogicSystem::heartbeatHandler(const std::shared_ptr<CSession>& session, const std::string& /*message_data*/) const
{
    Json::Value return_value;
    Defer defer([this, &return_value, session]()
        {
            (void) this;
            const std::string return_str = return_value.toStyledString();
            session->send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_HEART_BEAT_RSP);
        });

    const std::uint64_t uid = session->getUserId();
    if (uid == 0)
    {
        // 未登录（尚未绑定 uid）的 session 不允许续期，避免为匿名连接刷新在线状态
        spdlog::warn("[logic system] heartbeat before login, session: {}", session->getSessionId());
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }

    // 续期在线状态 key：30 秒内没有再收到心跳，key 自动过期，isUserOnLine 即判定用户离线
    const auto ip_key = USER_IP_PERFIX + std::to_string(uid);
    if (!RedisManager::GetInstance().set(ip_key, m_selfServerName, HEART_BEAT_EXPIRE_SECONDS))
    {
        spdlog::warn("[logic][LogicSystem::heartbeatHandler] Redis insert error, uid: {}", uid);
        return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        return;
    }

    return_value["error"] = ErrorCode::SUCCESS;
}

void LogicSystem::searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SEARCH_USER_RSP)

    // 客户端 uid 用字符串传递；非字符串按空串处理，避免 jsoncpp 抛异常终止逻辑线程
    const Json::Value& search_value = root["uid"];
    const auto user_name_or_uid = search_value.isString() ? search_value.asString() : std::string(); // 可能是 uid 也有可能是 用户名

    spdlog::debug("[logic system] user search, search value is: {}", user_name_or_uid);

    std::uint64_t uid{};
    const auto [ptr, ec] = std::from_chars(
        user_name_or_uid.c_str(), user_name_or_uid.c_str() + user_name_or_uid.size(), uid);
    // 必须同时满足无错误且全部字符被消费：空串在 from_chars 下同样满足 ptr == end，
    // 但没有 ec 判断时会以 uid=0 去查一次库
    if (ec == std::errc() && ptr == user_name_or_uid.c_str() + user_name_or_uid.size()) // 是纯数字，先找uid
    {
        auto info = SearchUserByUid(uid);
        if (info) // 找到
        {
            return_value["error"] = ErrorCode::SUCCESS;
            return_value["uid"] = std::to_string(info->uid); // uid 对外统一为字符串
            return_value["user_name"] = info->name;
            return_value["desc"] = info->desc;
            return_value["sex"] = info->sex;
            return_value["icon"] = info->icon;
            return;
        }
    }
    // 继续找用户名
    auto infos = SearchUserByName(user_name_or_uid);

    if (infos.empty())
    {
        // 都没找到
        return_value["error"] = ErrorCode::USER_NOT_FOUND;
        return;
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

void LogicSystem::sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_ADD_FRIEND_RSP)

    std::uint64_t form_uid{};
    std::uint64_t to_uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), form_uid) ||
        !uid_codec::ParseUid(root["to_uid"].asString(), to_uid))
    {
        spdlog::debug("[logic system] add friend request, parse uid failed, str is {} and {}", root["uid"].asString(), root["to_uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }
    if (form_uid != session->getUserId())
    {
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }
    spdlog::debug("[logic system] add friend request, from {} to {}", form_uid, to_uid);
    const std::string verify_info = root["verify_info"].asString();
    const std::string remark = root["remark"].asString();

    std::uint64_t add_frined_request_id{};
    const auto code = MySqlManager::GetInstance().sendFriendRequest(add_frined_request_id, form_uid, to_uid, verify_info, remark);
    if (code < 0) // 异常处理
    {
        if (code == -1 || code == -2) // 这里拦截用户不存在
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
            spdlog::warn("[LogicSystem::sendFriendRequestHandler] recvive a unexist uid form_uid: {} to_uid: {}", form_uid, to_uid);
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
        return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["add_friend_request_id"] = std::to_string(add_frined_request_id);
    // redis查找发送用户验证消息，如果用户在线，通过grpc发送验证请求
    const auto user_ip = RedisManager::GetInstance().isUserOnLine(to_uid);
    if (user_ip.empty())
    {
        return; // 对方不在线
    }
    if (user_ip == m_selfServerName)
    {
        // 在当前服务器
        const auto session_other = UserManager::GetInstance().getSession(to_uid);
        if (!session_other)
        {
            return;
        }
        session_other->sendAuthFriendRequest(add_frined_request_id);
        spdlog::debug("LogicSystem::sendFriendRequestHandler send friend request to {}, friend request id {}", to_uid, add_frined_request_id);
        return;
    }
    (void) GrpcSendFriendRequestToOther(add_frined_request_id, to_uid, user_ip);
}

void LogicSystem::syncFriendRequestListHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_FRIEND_REQUEST_RSP)

    std::uint64_t uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), uid))
    {
        spdlog::debug("[logic system] sync friend request list, parse uid failed, str is {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }
    if (uid != session->getUserId())
    {
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }
    spdlog::debug("[logic system] sync friend request list, uid: {}", uid);

    std::vector<std::shared_ptr<ApplyInfo> > request_infos = MySqlManager::GetInstance().getFriendRequestListByUid(uid);

    return_value["error"] = ErrorCode::SUCCESS;
    return_value["count"] = std::to_string(request_infos.size());

    for (const auto& info: request_infos)
    {
        Json::Value value;
        value["id"] = std::to_string(info->add_friend_request_id);
        value["from_uid"] = std::to_string(info->from_uid);
        value["to_uid"] = std::to_string(info->to_uid);
        value["name"] = info->name;
        value["verify_info"] = info->desc;
        value["icon"] = info->icon;
        value["sex"] = info->sex;
        value["state"] = info->status;
        value["nick"] = info->nick; // 没有备注或是接收方则 nick备注 为空字符串
        return_value["apply_infos"].append(value);
    }
}

void LogicSystem::checkIsUserOnlineHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_CHECK_IS_USER_ONLINE_RSP)

    std::uint64_t uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), uid))
    {
        spdlog::debug("[logic system] check user online request, parse uid failed, str is {}", root["uid"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }
    spdlog::debug("[logic system] check user online request, check the uid {}", uid);

    const auto ip_key = USER_IP_PERFIX + std::to_string(uid);
    std::string value;
    const auto isOnline = RedisManager::GetInstance().get(ip_key, value);
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["online"] = isOnline ? "online" : "offline";
}

void LogicSystem::authFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_AUTH_FRIEND_RSP)

    return_value["add_friend_request_id"] = root["add_friend_request_id"].asString();
    std::uint64_t add_friend_request_id{};
    if (!uid_codec::ParseUid(root["add_friend_request_id"].asString(), add_friend_request_id))
    {
        spdlog::debug("[logic system] authFriendRequestHandler, parse add_friend_request_id failed, str is {}", root["add_friend_request_id"].asString());
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }
    int state{};
    if (!uid_codec::ParseNumber<int>(root["state"].asString(), state) ||
        state != 1 && state != 2) // 只能是接受(1)或拒绝(2)
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }
    spdlog::debug("[logic system] authFriendRequestHandler, id: {} state: {}", add_friend_request_id, state);
    std::uint64_t sender_uid{}; // 这条好友申请的发送者
    const auto code = MySqlManager::GetInstance().handleFriendRequest(sender_uid, add_friend_request_id, session->getUserId(), static_cast<FriendApplyStatus>(state));
    if (code < 0)
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
        return;
    }

    return_value["error"] = ErrorCode::SUCCESS;


    // redis查找发送用户验证消息，如果用户在线，通过grpc发送验证请求
    const auto user_ip = RedisManager::GetInstance().isUserOnLine(sender_uid);
    if (user_ip.empty())
    {
        return; // 对方不在线
    }
    if (user_ip == m_selfServerName)
    {
        // 在当前服务器
        const auto session_other = UserManager::GetInstance().getSession(sender_uid);
        if (!session_other)
        {
            return;
        }
        session_other->sendFriendRequestResult(add_friend_request_id, state);
        spdlog::debug("LogicSystem::authFriendRequestHandler send auth friend request to {}, status {}", sender_uid, state);
        return;
    }

    (void) GrpcAuthFriendRequestAndNotifyToOther(sender_uid, add_friend_request_id, state, user_ip);
}

void LogicSystem::sendTextMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
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
        return;
    }

    bool is_group = false; //TODO 判断是否为群聊

    RedisManager::GetInstance().insertMessageToOffline({
        .type        = static_cast<chat_message::Type>(message_type),
        .from_uid    = session->getUserId(),
        .to_uid      = to_uid,
        .is_to_group = is_group,
        .body        = root["body"].asString(),
        .message_id  = {} // 由 redis 生成
    });

    return_value["error"] = ErrorCode::SUCCESS;

    // 通知对方有消息
    const auto user_ip = RedisManager::GetInstance().isUserOnLine(to_uid);
    if (user_ip.empty())
    {
        return; // 对方不在线
    }
    if (user_ip == m_selfServerName)
    {
        // 在当前服务器
        const auto session_other = UserManager::GetInstance().getSession(to_uid);
        if (!session_other)
        {
            return;
        }
        session_other->notifyReceiveMessage();
        return;
    }

    (void) GrpcNotifyReceiveMessageToOther(to_uid, user_ip);
}

void LogicSystem::syncUserFriendList(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_FRIEND_LIST_RSP)

    if (session->getUserId() == 0) // session未登录
    {
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }
    const auto friend_list_only_uid_and_remark = MySqlManager::GetInstance().getUserFriendList(session->getUserId());

    for (const auto& [remark, uid]: friend_list_only_uid_and_remark)
    {
        const auto friend_info = SearchUserByUid(uid);
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

void LogicSystem::syncChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_SYNC_CHAT_MESSAGE_RSP)

    std::uint64_t count{};
    if (!uid_codec::ParseNumber(root["count"].asString(), count) || count > 10) // 不能一次拉取超过10条
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }

    const auto messages = RedisManager::GetInstance().getMessageFromOffline(session->getUserId(), count); // 拉取 count 条
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["count"] = messages.size();
    for (const auto& message: messages)
    {
        if (!message.is_to_group)
        {
            Json::Value value;
            value["type"] = message.type;
            value["from_uid"] = std::to_string(message.from_uid);
            value["body"] = message.body;
            value["id"] = message.message_id; // redis 自动生成的唯一消息id（毫秒时间戳-序列号）
            return_value["msgs"].append(value);
        }
        else
        {
            // TODO 群聊消息待完善
            continue;
        }
    }
}

void LogicSystem::ackChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    HANDLER_PREFIX(MESSAGE_ACK_CHAT_MESSAGE_RSP)

    if (!root["ids"].isArray() || root["ids"].size() > 10) // 不能一次确认10条以上
    {
        return_value["error"] = ErrorCode::REQUEST_VALUE_INVALID;
        return;
    }

    std::vector<std::string> message_ids;
    for (const auto& id: root["ids"])
    {
        message_ids.push_back(id.asString());
    }

    RedisManager::GetInstance().ackAndDelChatMessages(session->getUserId(), message_ids);
    return_value["error"] = ErrorCode::SUCCESS;
}

bool LogicSystem::GrpcSendFriendRequestToOther(const std::uint64_t add_friend_request_id, const std::uint64_t to_uid, const std::string& user_ip)
{
    AddFriendReq req;
    req.set_touid(to_uid);
    req.set_addfriendrequestid(add_friend_request_id);
    const auto rsp = ChatGrpcClient::GetInstance().addFriend(user_ip, req);
    if (rsp.error() != 0)
    {
        spdlog::warn("LogicSystem::GrpcSendFriendRequestToOther send grpc error, error: {}", rsp.error());
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
        spdlog::warn("LogicSystem::GrpcAuthFriendRequestAndNotifyToOther send grpc error, error: {}", rsp.error());
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
        spdlog::warn("LogicSystem::GrpcNotifyReceiveMessageToOther send grpc error, error: {}", rsp.error());
        return false;
    }
    return true;
}

std::shared_ptr<UserInfo> LogicSystem::SearchUserByUid(const uint64_t uid)
{
    // redis 查询
    const auto base_key = USER_BASE_INFO + std::to_string(uid);
    std::string info_str;
    if (RedisManager::GetInstance().get(base_key, info_str))
    {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        return std::make_shared<UserInfo>(
            std::string(),
            root["name"].asString(),
            root["email"].asString(),
            root["desc"].asString(),
            root["icon"].asString(),
            root["uid"].asUInt64(),
            static_cast<Sex>(root["sex"].asUInt())
            );
    }
    // 查数据库
    const auto user_info = MySqlManager::GetInstance().getUser(uid);

    RedisManager::GetInstance().insert(user_info);
    return user_info;
}

std::vector<std::shared_ptr<UserInfo> > LogicSystem::SearchUserByName(const std::string& name)
{
    // redis 查询
    const auto base_key = USER_BASE_INFO_BY_NAME + name;
    std::string infos_str;
    std::vector<std::shared_ptr<UserInfo> > user_infos;
    if (RedisManager::GetInstance().get(base_key, infos_str))
    {
        Json::Reader reader;
        Json::Value root;
        reader.parse(infos_str, root);
        for (const auto& i: root["uids"])
        {
            user_infos.push_back(SearchUserByUid(i.asUInt64()));
        }
        return user_infos;
    }
    // 查数据库
    user_infos = MySqlManager::GetInstance().getUser(name);

    RedisManager::GetInstance().insert(user_infos);
    return user_infos;
}
