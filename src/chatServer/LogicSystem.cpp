#include "LogicSystem.h"

#include <json/json.h>
#include <json/reader.h>

#include <Defer.h>
#include <sql/mysql/MySqlManager.h>

#include "utils/config.h"
#include "RedisManager.h"
#include "data/UserManager.h"
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
                    message_node->m_receviceNdoe->m_messageId,
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
            message_node->m_receviceNdoe->m_messageId,
            std::string(message_node->m_receviceNdoe->m_data, message_node->m_receviceNdoe->m_currentLen));
        m_messageQueue.pop();
        spdlog::info("[logic system] dealMessage() handle message id: {}", message_node->m_receviceNdoe->m_messageId);
    }
}


void LogicSystem::registerCallBacks()
{
    m_callbackFunctions[MessageType::MESSAGE_CHAT_LOGIN] = [this](
        const std::shared_ptr<CSession>& session,
        const short message_id,
        const std::string& message_data)
            {
                loginHandler(session, message_id, message_data);
            };
}

void LogicSystem::loginHandler(const std::shared_ptr<CSession>& session, const short message_id, const std::string& message_data) const
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message_data, root);
    auto uid = root["uid"].asUInt64();
    auto token = root["token"].asString();
#ifdef CHAT_DEBUG
    spdlog::debug("[logic system] user login, id: {}, token: {}", uid, token);
#else
    spdlog::info("[logic system] user login, id: {}", uid);
#endif
    Json::Value return_value;
    Defer defer([this, &return_value, session]()
        {
            (void) this;
            const std::string return_str = return_value.toStyledString();
            session->send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_CHAT_LOGIN_RESP);
        });

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

    return_value["uid"] = uid;
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

void LogicSystem::getUserApplyListAndFriendList(const std::shared_ptr<CSession>&, short message_id, const std::string& message_data)
{
    //TODO
}
