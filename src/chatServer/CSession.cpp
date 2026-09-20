#include "CSession.h"

#include <boost/uuid.hpp>

#include "LogicSystem.h"
#include "CServer.h"

namespace chat
{
CSession::CSession(boost::asio::io_context& io_context, CServer* server)
    : m_data{}, m_socket(io_context), m_server(server), m_beClose(false), m_beHeadParse(false)
{
    const auto uuid = boost::uuids::random_generator()();
    m_uuid = to_string(uuid);
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

std::string CSession::getUuid()
{
    return m_uuid;
}

tcp::socket& CSession::getSocket()
{
    return m_socket;
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
                if (error_code)
                {
                    spdlog::warn("[tcp session] tcp head read error: {}", error_code.what());
                    close();
                    m_server->clearSession(m_uuid);
                    return;
                }
                if (bytes_transferred < HEAD_TOTAL_LEN)
                {
                    spdlog::error("[tcp session] tcp head_len not match {} != {} !!!", bytes_transferred, HEAD_TOTAL_LEN);
                    close();
                    m_server->clearSession(m_uuid);
                    return;
                }

                m_receiveHeadNode->clear();
                memcpy(m_receiveHeadNode->m_data, m_data, bytes_transferred);
                // 获取messageid
                short message_id = 0;
                memcpy(&message_id, m_receiveHeadNode->m_data, HEAD_ID_LEN);
                // 转换本地字节序
                message_id = boost::asio::detail::socket_ops::network_to_host_short(message_id);
                // id非法
                if (message_id > MAX_LENGTH)
                {
                    spdlog::warn("[tcp session] receive a invalid message id: {}", message_id);
                    m_server->clearSession(m_uuid);
                    return;
                }
                short message_len = 0;
                memcpy(&message_len, m_receiveHeadNode->m_data + HEAD_ID_LEN, HEAD_DATA_LEN);
                message_len = boost::asio::detail::socket_ops::network_to_host_short(message_len);
                // 长度非法
                if (message_len > MAX_LENGTH)
                {
                    spdlog::warn("[tcp session] receive a invalid message len: {}", message_len);
                    m_server->clearSession(m_uuid);
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
                    m_server->clearSession(m_uuid);
                    return;
                }
                if (bytes_transferred < message_len)
                {
                    spdlog::error("[tcp session] tcp body_len not match {} != {} !!!", bytes_transferred, message_len);
                    close();
                    m_server->clearSession(m_uuid);
                    return;
                }

                memcpy(m_receiveMessageNode->m_data, m_data, bytes_transferred);
                m_receiveMessageNode->m_currentLen += bytes_transferred;
                m_receiveMessageNode->m_data[m_receiveMessageNode->m_totalLen] = '\0';
                spdlog::info("[tcp session] message is: {}", m_receiveMessageNode->m_data);

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
        spdlog::warn("[tcp session] session[{}] send queue fulled, size is: {}", m_uuid, MAX_SEND_QUEUE_LEN);
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
        if (error_code)
        {
            spdlog::warn("[tcp session] handle write error: {}", error_code.what());
        }

        std::lock_guard<std::mutex> lock(m_sendMutex);
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

void CSession::close()
{
    m_beClose = true;
}
}
