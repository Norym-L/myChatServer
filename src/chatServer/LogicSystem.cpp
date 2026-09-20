#include "LogicSystem.h"

#include <json/json.h>
#include <json/reader.h>

#include "CSession.h"

namespace chat
{
LogicSystem::LogicSystem()
    : m_beStop(false)
{
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
    // 发送通知信号
    if (messageQueue_size > 0)
    {
        lock.unlock();
        m_conditionVariable.notify_one();
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
    m_callbackFunctions[MessageType::MESSAGE_CHAT_LOGIN] = std::bind(
        &LogicSystem::loginHandler,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
}

void LogicSystem::loginHandler(std::shared_ptr<CSession>, const short& message_id, const std::string& message_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message_data, root);
    auto uid = root["uid"].asUInt64();
    spdlog::info("[logic system] user login, id: {}, token: {}", uid, root["token"].asString());
    //TODO 从状态服务器获取token检查是否匹配
}
}
