#include "CSession.h"

#include <boost/uuid.hpp>
#include <json/value.h>

#include "../LogicSystem.h"
#include "../CServer.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
    : m_data{}, m_socket(io_context), m_server(server)
{
    const auto uuid = boost::uuids::random_generator()();
    m_sessionId = to_string(uuid);
    m_receiveHeadNode = std::shared_ptr<MessageNode>(new MessageNode(HEAD_TOTAL_LEN));
}

CSession::~CSession()
{
    spdlog::info("[server] session destruct");
}


void CSession::start()
{
    asyncReadHead(HEAD_TOTAL_LEN);
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

void CSession::asyncReadHead(std::uint8_t head_len)
{
    auto self = shared_from_this();

    // 必须完 HEAD_TOTAL_LEN 才会触发回调
    asyncReadFull(
        HEAD_TOTAL_LEN,
        [self, this](const boost::system::error_code& error_code, std::size_t bytes_transferred)
            {
                try
                {
                    if (error_code == boost::asio::error::eof)
                    {
                        // 对端正常关闭连接，这是一个正常的结束信号
                        spdlog::info("[tcp session] the remote host closed the connection. id: {}", m_sessionId);
                        close();
                        m_server->clearSession(m_sessionId);
                        return;
                        return;
                    }
                    if (error_code)
                    {
                        spdlog::warn("[tcp session] tcp head read error: {}", error_code.what());
                        close();
                        m_server->clearSession(m_sessionId);
                        return;
                    }
                    if (bytes_transferred < HEAD_TOTAL_LEN)
                    {
                        spdlog::error("[tcp session] tcp head_len not match {} != {} !!!", bytes_transferred, HEAD_TOTAL_LEN);
                        close();
                        m_server->clearSession(m_sessionId);
                        return;
                    }

                    m_receiveHeadNode->clear();
                    memcpy(m_receiveHeadNode->m_data, m_data, bytes_transferred);
                    // 获取messageid
                    short message_id = 0;
                    memcpy(&message_id, m_receiveHeadNode->m_data, HEAD_ID_LEN);
                    // 转换本地字节序
                    message_id = static_cast<short>(boost::asio::detail::socket_ops::network_to_host_short(message_id));
                    // id非法
                    if (message_id > MAX_LENGTH)
                    {
                        spdlog::warn("[tcp session] receive a invalid message id: {}", message_id);
                        m_server->clearSession(m_sessionId);
                        return;
                    }
                    short message_len = 0;
                    memcpy(&message_len, m_receiveHeadNode->m_data + HEAD_ID_LEN, HEAD_DATA_LEN);
                    message_len = static_cast<short>(boost::asio::detail::socket_ops::network_to_host_short(message_len));
                    // 长度非法
                    if (message_len > MAX_LENGTH)
                    {
                        spdlog::warn("[tcp session] receive a invalid message len: {}", message_len);
                        m_server->clearSession(m_sessionId);
                        return;
                    }
                    spdlog::info("[tcp session] receive message id: {}, len: {}", message_id, message_len);

                    m_receiveMessageNode = std::shared_ptr<ReceiveNode>(new ReceiveNode(message_len, message_id));
                    AsyncReadBody(message_len);
                }
                catch (std::exception& e)
                {
                    spdlog::warn("[tcp session] tcp head read error: {}", e.what());
                }
            });
}

void CSession::AsyncReadBody(size_t message_len)
{
    auto self = shared_from_this();
    asyncReadFull(
        message_len,
        [self, this, message_len](const boost::system::error_code& error_code, std::size_t bytes_transferred)
            {
                try
                {
                    if (error_code)
                    {
                        spdlog::warn("[tcp session] tcp body read error: {}", error_code.what());
                        close();
                        m_server->clearSession(m_sessionId);
                        return;
                    }
                    if (bytes_transferred < message_len)
                    {
                        spdlog::error("[tcp session] tcp body_len not match {} != {} !!!", bytes_transferred, message_len);
                        close();
                        m_server->clearSession(m_sessionId);
                        return;
                    }

                    memcpy(m_receiveMessageNode->m_data, m_data, bytes_transferred);
                    m_receiveMessageNode->m_currentLen += bytes_transferred;
                    m_receiveMessageNode->m_data[m_receiveMessageNode->m_totalLen] = '\0';
                    // 将消息添加到逻辑处理队列中
                    LogicSystem::GetInstance().postMessageToQueue(std::shared_ptr<LogicNode>(new LogicNode(shared_from_this(), m_receiveMessageNode)));
                    // 继续监听
                    asyncReadHead(HEAD_TOTAL_LEN);
                }
                catch (std::exception& e)
                {
                    spdlog::warn("[tcp session] tcp body read error: {}", e.what());
                }
            });
}

void CSession::asyncReadFull(const std::size_t len, const std::function<void(const boost::system::error_code&, std::size_t)>& handle)
{
    ::memset(m_data, 0, MAX_LENGTH);
    asyncReadLen(0, len, handle);
}

void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len, const std::function<void(const boost::system::error_code&, std::size_t)>& handle)
{
    auto self = shared_from_this();
    m_socket.async_read_some(
        boost::asio::buffer(m_data + read_len, total_len - read_len),
        [read_len, total_len, handle, self](const boost::system::error_code& error_code,
                                            const std::size_t bytes_transferred)
            {
                if (error_code)
                {
                    handle(error_code, read_len + bytes_transferred);
                    return;
                }

                if (read_len + bytes_transferred >= total_len)
                {
                    // 长度够了
                    handle(error_code, read_len + bytes_transferred);
                    return;
                }
                // 长度不够
                self->asyncReadLen(read_len + bytes_transferred, total_len, handle);
            });
}

void CSession::send(const char* message, const size_t len, const short message_id)
{
    std::lock_guard<std::mutex> lock(m_sendMutex);
    const size_t sendQueue_size = m_sendQueue.size();
    if (sendQueue_size > MAX_SEND_QUEUE_LEN)
    {
        // 队列积压说明对端不再取数据（TCP 背压），继续静默丢包会让客户端一直等响应；直接关闭会话
        spdlog::warn("[tcp session] session[{}] send queue fulled, size is: {}, close session", m_sessionId, MAX_SEND_QUEUE_LEN);
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
            spdlog::warn("[tcp session] handle write error: {}, drop {} pending message(s)", error_code.what(), m_sendQueue.size());
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
        spdlog::warn("[tcp session] handle write error: {}", e.what());
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
        // Redis 命令只能由逻辑线程发起：这里只投递任务，不阻塞连接线程
        LogicSystem::GetInstance().offlineUser(m_userId);
    }
    // socket 为共享资源：send() 会在逻辑线程发起 async_write，关闭操作必须与之互斥，避免并发操作同一 socket
    std::lock_guard<std::mutex> lock(m_sendMutex);
    boost::system::error_code ec;
    m_socket.close(ec);
}
