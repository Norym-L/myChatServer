#include "CSession.h"

#include <boost/uuid.hpp>
#include <json/value.h>

#include "../LogicSystem.h"
#include "../CServer.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
    : m_headData{}, m_socket(io_context), m_server(server)
{
    const auto uuid = boost::uuids::random_generator()();
    m_sessionId = to_string(uuid);
}

CSession::~CSession()
{
    SPDLOG_INFO("[server] session destruct");
}


void CSession::start()
{
    auto self = shared_from_this();
    net::co_spawn(
        m_socket.get_executor(),
        [self]() -> net::awaitable<void>
            {
                co_await self->readLoop();
            },
        [self](const std::exception_ptr& ep)
            {
                if (ep)
                {
                    try { std::rethrow_exception(ep); }
                    catch (const boost::system::system_error& e)
                    {
                        if (e.code() != net::error::eof &&
                            e.code() != net::error::operation_aborted)
                            SPDLOG_WARN("[tcp session] read loop: {}", e.what());
                    }
                    catch (const std::exception& e)
                    {
                        SPDLOG_ERROR("[tcp session] read loop unexpected: {}", e.what());
                    }
                    catch (...)
                    {
                        SPDLOG_ERROR("[tcp session] read loop unknown exception");
                    }
                }
                self->close();
                self->m_server->clearSession(self->m_sessionId);
            });
}

std::string CSession::getSessionId()
{
    return m_sessionId;
}

tcp::socket& CSession::getSocket()
{
    return m_socket;
}

void CSession::setUserId(const std::uint64_t uid)
{
    m_userId = uid;
}

std::uint64_t CSession::getUserId() const
{
    return m_userId;
}

net::awaitable<void> CSession::readLoop()
{
    for (;;)
    {
        co_await net::async_read(
            m_socket,
            net::buffer(m_headData, HEAD_TOTAL_LEN),
            net::use_awaitable);
        std::uint16_t message_id, message_len;
        ::memcpy(&message_id, m_headData, HEAD_ID_LEN);
        ::memcpy(&message_len, m_headData + HEAD_ID_LEN, HEAD_DATA_LEN);
        message_id = boost::asio::detail::socket_ops::network_to_host_short(message_id);
        message_len = boost::asio::detail::socket_ops::network_to_host_short(message_len);
        if (message_id > MAX_ID_NUMBER || message_len > MAX_LENGTH) // message_len 为0也放行，保证心跳消息可以接收
        {
            SPDLOG_WARN("[tcp session] invalid head id={} len={}", message_id, message_len);
            co_return;
        }
        const auto message = std::make_shared<ReceiveNode>(message_len, message_id);
        co_await net::async_read(
            m_socket,
            net::buffer(message->data, message_len),
            net::use_awaitable);
        message->currentLen = message_len;

        LogicSystem::GetInstance().postMessageToQueue(std::make_shared<LogicNode>(shared_from_this(), message));
    }
}

void CSession::send(const char* message, const size_t len, const short message_id)
{
    std::lock_guard<std::mutex> lock(m_sendMutex);
    const size_t sendQueue_size = m_sendQueue.size();
    if (sendQueue_size > MAX_SEND_QUEUE_LEN)
    {
        // 队列积压说明对端不再取数据（TCP 背压），继续静默丢包会让客户端一直等响应；直接关闭会话
        SPDLOG_WARN("[tcp session] session[{}] send queue fulled, size is: {}, close session", m_sessionId, MAX_SEND_QUEUE_LEN);
        boost::system::error_code ec;
        m_socket.close(ec);
        return;
    }

    m_sendQueue.push(std::shared_ptr<SendNode>(new SendNode(message, len, message_id)));

    if (sendQueue_size > 0)
    {
        return;
    }
    const auto& message_node = m_sendQueue.front();
    boost::asio::async_write(
        m_socket,
        boost::asio::buffer(message_node->m_data, message_node->m_totalLen),
        std::bind(&CSession::handleWrite, this, std::placeholders::_1, this->shared_from_this()));
}

void CSession::handleWrite(const boost::system::error_code& error_code, std::shared_ptr<CSession> shared_self)
{
    try
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (error_code)
        {
            // 写失败说明连接已不可用：丢弃剩余队列并关闭 socket，避免继续向坏 socket 投递
            // （随后的读操作会因 socket 关闭而失败，由读回调负责 clearSession 清理会话登记）
            SPDLOG_WARN("[tcp session] handle write error: {}, drop {} pending message(s)", error_code.what(), m_sendQueue.size());
            std::queue<std::shared_ptr<SendNode> > empty_queue;
            std::swap(m_sendQueue, empty_queue);
            boost::system::error_code ec;
            m_socket.close(ec);
            return;
        }

        m_sendQueue.pop();
        if (!m_sendQueue.empty())
        {
            const auto& message_node = m_sendQueue.front();
            boost::asio::async_write(
                m_socket,
                boost::asio::buffer(message_node->m_data, message_node->m_totalLen),
                std::bind(&CSession::handleWrite, this, std::placeholders::_1, shared_self));
        }
    }
    catch (std::exception& e)
    {
        SPDLOG_WARN("[tcp session] handle write error: {}", e.what());
    }
}

void CSession::sendAuthFriendRequest(const std::uint64_t add_friend_request_id)
{
    Json::Value return_value;
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["add_friend_request_id"] = std::to_string(add_friend_request_id);
    const std::string return_str = return_value.toStyledString();
    send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_NOTIFY_ADD_FRIEND_REQ);
}

void CSession::sendFriendRequestResult(const std::uint64_t add_friend_request_id, const int state)
{
    Json::Value return_value;
    return_value["error"] = ErrorCode::SUCCESS;
    return_value["add_friend_request_id"] = std::to_string(add_friend_request_id);
    return_value["state"] = state;
    const std::string return_str = return_value.toStyledString();
    send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_NOTIFY_AUTH_FRIEND_REQ);
}

void CSession::sendMessage(chat_message::Message message)
{
    Json::Value return_value;
    return_value["group"] = message.is_to_group;
    if (message.is_to_group && message.to_uid != 0) // 升级前写入的群消息没有 group_id
    {
        return_value["group_id"] = std::to_string(message.to_uid);
    }
    return_value["type"] = message.type;
    return_value["from_uid"] = std::to_string(message.from_uid);
    return_value["body"] = message.body;
    return_value["time"] = message.message_id; // redis 自动生成的唯一消息id（毫秒时间戳-序列号）
    const std::string return_str = return_value.toStyledString();
    send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_CHAT_MESSAGE);
}

void CSession::notifyReceiveMessage()
{
    Json::Value return_value;
    return_value["error"] = ErrorCode::SUCCESS;
    const std::string return_str = return_value.toStyledString();
    send(return_str.c_str(), return_str.size(), MessageType::MESSAGE_NOTIFY_RECEIVE_MESSAGE);
}

void CSession::close()
{
    if (m_userId != 0)
    {
        // Redis 命令只能由逻辑线程发起：这里只投递任务，不阻塞连接线程。
        // 带上 sessionId：旧连接晚于新登录断开时，不能删掉新会话的 uip_<uid>。
        LogicSystem::GetInstance().offlineUser(m_userId, m_sessionId);
    }
    // socket 为共享资源：send() 会在逻辑线程发起 async_write，关闭操作必须与之互斥，避免并发操作同一 socket
    std::lock_guard<std::mutex> lock(m_sendMutex);
    boost::system::error_code ec;
    m_socket.close(ec);
}
