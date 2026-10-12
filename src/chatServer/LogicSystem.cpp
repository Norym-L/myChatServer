#include "LogicSystem.h"

#include <cstdint>

#include <json/json.h>
#include <json/reader.h>

#include <Defer.h>
#include <sql/mysql/MySqlManager.h>
#include <utils/UidCodec.h>

#include "utils/config.h"
#include "RedisManager.h"
#include "data/UserManager.h"
#include "grpc/ChatGrpcClient.h"
#include "net/CSession.h"
#include "statusClient/StatusGrpcClient.h"

LogicSystem::LogicSystem()
    : m_beStop(false)
{
    RedisManager::GetInstance();
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
        if (!RedisManager::GetInstance().set(base_key, root.toStyledString()))
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
}

void LogicSystem::loginHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message_data, root);
    // 客户端 uid 统一为字符串：非字符串或非纯数字一律按非法 uid 处理，避免 jsoncpp 抛异常
    const Json::Value& uid_value = root["uid"];
    const auto token = root["token"].asString();

    Json::Value return_value;
    Defer defer([this, &return_value, session]()
        {
            (void) this;
            const std::string return_str = return_value.toStyledString();
            session->send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_CHAT_LOGIN_RSP);
        });

    std::uint64_t uid{0};
    if (!uid_value.isString() || !uid_codec::ParseUid(uid_value.asString(), uid))
    {
        spdlog::warn("[logic system] login with invalid uid: {}", root["uid"].toStyledString());
        return_value["error"] = ErrorCode::USER_UID_INVALID;
        return;
    }

    spdlog::debug("[logic system] user login, id: {}, token: {}", uid, token);

    // auto rsp = StatusGrpcClient::GetInstance().login(uid, token);
    // return_value["error"] = rsp.error();
    // if (rsp.error() != static_cast<uint16_t>(ErrorCode::SUCCESS))
    // {
    //     return;
    // }

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

    // 从数据库获取好友列表和好友申请列表
    // TODO

    // 增加登录数量
    static const auto SELF_NAME = ResolveSelfSection(ConfigManager::GetInstance());
    auto res = RedisManager::GetInstance().hGet(LOGIN_COUNT, SELF_NAME);
    std::uint64_t count{0};
    if (!res.empty())
    {
        std::from_chars(res.c_str(), res.c_str() + res.length(), count);
    }
    ++count;
    if (!RedisManager::GetInstance().hSet(LOGIN_COUNT, SELF_NAME, std::to_string(count)))
    {
        spdlog::warn("[logic][LogicSystem::loginHandler] Redis insert error!");
    }

    // 绑定用户session
    session->setUserId(uid);

    // 设置用户登录的服务器
    const auto ip_key = USER_IP_PERFIX + uid_str;
    if (!RedisManager::GetInstance().set(ip_key, SELF_NAME))
    {
        spdlog::warn("[logic][LogicSystem::loginHandler] Redis insert error!");
    }

    // 绑定uid和session，实现踢人操作
    UserManager::GetInstance().setUserSession(uid, session);
}

void LogicSystem::searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message_data, root);
    // 客户端 uid 用字符串传递；非字符串按空串处理，避免 jsoncpp 抛异常终止逻辑线程
    const Json::Value& search_value = root["uid"];
    const auto user_name_or_uid = search_value.isString() ? search_value.asString() : std::string(); // 可能是 uid 也有可能是 用户名

    spdlog::debug("[logic system] user search, search value is: {}", user_name_or_uid);

    Json::Value return_value;
    Defer defer([this, &return_value, session]()
        {
            (void) this;
            const std::string return_str = return_value.toStyledString();
            session->send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_SEARCH_USER_RSP);
        });
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
    // TODO 多个结果返回, 暂时只返回第一个
    for (const auto& info: infos)
    {
        return_value["error"] = ErrorCode::SUCCESS;
        return_value["uid"] = std::to_string(info->uid); // uid 对外统一为字符串
        return_value["user_name"] = info->name;
        return_value["desc"] = info->desc;
        return_value["sex"] = info->sex;
        return_value["icon"] = info->icon;
        return;
    }

    // 都没找到
    return_value["error"] = ErrorCode::USER_NOT_FOUND;
    return_value["uid"] = "0";
}

void LogicSystem::sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message_data, root);

    std::uint64_t form_uid{};
    std::uint64_t to_uid{};
    if (!uid_codec::ParseUid(root["uid"].asString(), form_uid) ||
        !uid_codec::ParseUid(root["to_uid"].asString(), to_uid))
    {
        spdlog::warn("[logic system] add friend request, parse uid failed, str is {} and {}", root["uid"].asString(), root["to_uid"].asString());
    }
    spdlog::debug("[logic system] add friend request, from {} to {}", form_uid, to_uid);
    const std::string verify_info = root["verify_info"].asString();
    const std::string remark = root["remark"].asString();

    Json::Value return_value;
    Defer defer([this, &return_value, session]()
        {
            (void) this;
            const std::string return_str = return_value.toStyledString();
            session->send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_ADD_FRIEND_RSP);
        });

    const auto error_code = MySqlManager::GetInstance().sendFriendRequest(form_uid, to_uid, verify_info, remark);
    if (error_code != 0)
    {
        if (error_code == -1 || error_code == -2) // 这里拦截用户不存在
        {
            return_value["error"] = ErrorCode::USER_UID_INVALID;
            spdlog::warn("[LogicSystem::sendFriendRequestHandler] recvive a unexist uid form_uid: {} to_uid: {}", form_uid, to_uid);
        }
        else if (error_code == -3)
        {
            return_value["error"] = ErrorCode::CAN_NOT_ADD_YOURSELF_AS_A_FRIEND;
        }
        else if (error_code == -4)
        {
            return_value["error"] = ErrorCode::USER_IS_YOUR_FRIEND_BEFOR;
        }
        else if (error_code == -5)
        {
            return_value["error"] = ErrorCode::CHACK_YOUR_RECEIVED_FRIEND_REQUEST;
        }
        else if (error_code == -10)
        {
            return_value["error"] = ErrorCode::SERVICE_UNAVAILABLE;
        }
        return;
    }
    return_value["error"] = ErrorCode::SUCCESS;
    const auto user_self_info = SearchUserByUid(form_uid);
    (void) GrpcSendFriendRequestToOther(form_uid, to_uid, verify_info, user_self_info->name, user_self_info->icon);
}

void LogicSystem::getUserApplyListAndFriendList(const std::shared_ptr<CSession>&, const std::string& message_data)
{
    //TODO
}

bool LogicSystem::GrpcSendFriendRequestToOther(const std::uint64_t from_uid, const std::uint64_t to_uid, const std::string& verify_info, const std::string& name, const std::string& icon)
{
    // redis查找发送用户验证消息，如果用户在线，通过grpc发送验证请求
    const auto user_ip = RedisManager::GetInstance().isUserOnLine(to_uid);
    if (user_ip.empty())
    {
        return false;
    }
    AddFriendReq req;
    req.set_formuid(from_uid);
    req.set_touid(to_uid);
    req.set_desc(verify_info);
    req.set_icon(icon);
    req.set_name(name);
    const auto rsp = ChatGrpcClient::GetInstance().addFriend(user_ip, req);
    if (rsp.error() != 0)
    {
        spdlog::warn("LogicSystem::sendFriendRequestToOther send grpc error, error: {}", rsp.error());
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
