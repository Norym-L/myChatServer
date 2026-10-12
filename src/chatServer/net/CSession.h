#ifndef GATESERVER_CSESSION_H
#define GATESERVER_CSESSION_H

#include <string>
#include <boost/asio.hpp>
#include <mutex>
#include <queue>

#include <const.h>

using boost::asio::ip::tcp;

class CServer;
class ReceiveNode;
class MessageNode;
class SendNode;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    ~CSession();

    void start();
    [[nodiscard]] std::string getSessionId();
    [[nodiscard]] tcp::socket& getSocket();
    // 绑定用户id
    void setUserId(std::uint64_t uid);
    [[nodiscard]] std::uint64_t getUserId() const;
    void asyncReadHead(std::uint8_t head_len = HEAD_TOTAL_LEN);
    void AsyncReadBody(size_t message_len);

    /// 读取完整长度
    void asyncReadFull(size_t len, const std::function<void(const boost::system::error_code&, size_t)>& handle);
    /// 读取指定的字节数
    void asyncReadLen(size_t read_len, size_t total_len, const std::function<void(const boost::system::error_code&, size_t)>& handle);

    void send(const char* message, size_t len, short message_id);
    void handleWrite(const boost::system::error_code& error_code, std::shared_ptr<CSession> shared_self);

    /**
     * 给当前session绑定用户发送好友验证申请（接收到好友请求）
     * @param from_uid 发送者的id
     * @param name 发送者名字
     * @param verify_info 验证信息
     * @param icon 发送者头像
     */
    void sendAuthFriendRequest(std::uint64_t from_uid, const std::string& name, const std::string& verify_info, const std::string& icon);

    void close();

private:
    char m_data[MAX_LENGTH];
    tcp::socket m_socket;
    CServer* m_server;
    std::string m_sessionId;
    std::shared_ptr<MessageNode> m_receiveHeadNode;
    std::shared_ptr<ReceiveNode> m_receiveMessageNode;
    std::mutex m_sendMutex;
    std::queue<std::shared_ptr<SendNode> > m_sendQueue;
    std::uint64_t m_userId{0};
};


class MessageNode
{
    friend class CSession;

public:
    ~MessageNode()
    {
        ::free(m_data);
    }

private:
    explicit MessageNode(const std::uint8_t len = HEAD_TOTAL_LEN)
        : m_len(len)
    {
        m_data = static_cast<char*>(::malloc(sizeof(char) * len));
    }

    void clear() const
    {
        ::memset(m_data, 0, sizeof(char) * m_len);
    }

    char* m_data;
    size_t m_len;
};


class ReceiveNode
{
    friend class CSession;
    friend class LogicSystem;

public:
    ~ReceiveNode()
    {
        ::free(m_data);
    }

private:
    explicit ReceiveNode(const size_t len, const short id)
        : m_messageId(id), m_totalLen(len)
    {
        // 多分配 1 字节以容纳结尾 '\0'（CSession.cpp 会写 m_data[m_totalLen]）
        m_data = static_cast<char*>(::malloc(sizeof(char) * (len + 1)));
    }

    char* m_data;
    short m_messageId;
    size_t m_currentLen = 0;
    size_t m_totalLen = MAX_LENGTH - 1;
};

class SendNode
{
    friend class CSession;
    friend class LogicSystem;

public:
    ~SendNode()
    {
        ::free(m_data);
    }

private:
    explicit SendNode(const char* message, const size_t len, const short message_id)
        : m_totalLen(HEAD_TOTAL_LEN + len)
    {
        // 发送缓冲区 = 4 字节协议头（2 字节 message_id + 2 字节报文长度，均网络字节序）+ 消息体
        m_data = static_cast<char*>(::malloc(sizeof(char) * (HEAD_TOTAL_LEN + len)));
        const auto id_network = boost::asio::detail::socket_ops::host_to_network_short(
            static_cast<unsigned short>(message_id));
        const auto len_network = boost::asio::detail::socket_ops::host_to_network_short(
            static_cast<unsigned short>(len));
        ::memcpy(m_data, &id_network, HEAD_ID_LEN);
        ::memcpy(m_data + HEAD_ID_LEN, &len_network, HEAD_DATA_LEN);
        ::memcpy(m_data + HEAD_TOTAL_LEN, message, len);
    }

    char* m_data;
    size_t m_totalLen;
};

class LogicNode
{
    friend class CSession;
    friend class LogicSystem;

private:
    LogicNode(const std::shared_ptr<CSession>& session, const std::shared_ptr<ReceiveNode>& receive_node)
        : m_session(session), m_receviceNdoe(receive_node)
    {}

    std::shared_ptr<CSession> m_session;
    std::shared_ptr<ReceiveNode> m_receviceNdoe;
};
#endif //GATESERVER_CSESSION_H
