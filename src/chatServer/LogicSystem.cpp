#include "LogicSystem.h"

#include <json/json.h>
#include <json/reader.h>

#include <Defer.h>
#include <sql/mysql/MySqlManager.h>

#include "RedisManager.h"
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

void LogicSystem::loginHandler(const std::shared_ptr<CSession>& session, const short message_id, const std::string& message_data)
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

    // 从 redis 中获取用户token
    std::string token_value{};
    if (!RedisManager::GetInstance().get(USER_TOKEN_PERFIX + std::to_string(uid), token_value))
    {
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

    // 内存中查询用户
    auto find_iter = m_users.find(uid);
    std::shared_ptr<UserInfo> user_info = nullptr;
    if (find_iter == m_users.end())
    {
        // 查数据库
        user_info = MySqlManager::GetInstance().getUser(uid);
        if (user_info == nullptr)
        {
            return_value["error"] = static_cast<uint16_t>(ErrorCode::USER_UID_INVALID);
            return;
        }

        user_info->token = token;
        m_users[uid] = user_info;
    }
    else
    {
        user_info = find_iter->second;
    }

    return_value["uid"] = uid;
    return_value["token"] = token_value;
    return_value["name"] = user_info->name;
}

