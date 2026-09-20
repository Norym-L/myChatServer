#ifndef GATESERVER_CSESSION_H
#define GATESERVER_CSESSION_H

#include <string>
#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <queue>

#include <const.h>

namespace chat
{
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
    [[nodiscard]] std::string getUuid();
    [[nodiscard]] tcp::socket& getSocket();
    void asyncReadHead(std::uint8_t head_len = HEAD_TOTAL_LEN);
    void AsyncReadBody(size_t message_len);

    /// 读取完整长度
    void asyncReadFull(size_t len, const std::function<void(const boost::system::error_code&, size_t)>& handle);
    /// 读取指定的字节数
    void asyncReadLen(size_t read_len, size_t total_len, const std::function<void(const boost::system::error_code&, size_t)>& handle);

    void send(const char* message, size_t len, short message_id);
    void handleWrite(const boost::system::error_code& error_code, std::shared_ptr<CSession> shared_self);


    void close();

private:
    char m_data[MAX_LENGTH];
    tcp::socket m_socket;
    CServer* m_server;
    std::string m_uuid;
    std::shared_ptr<MessageNode> m_receiveHeadNode;
    std::shared_ptr<ReceiveNode> m_receiveMessageNode;
    std::mutex m_sendMutex;
    std::queue<std::shared_ptr<SendNode> > m_sendQueue;
    bool m_beClose;
    bool m_beHeadParse;
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
        m_data = static_cast<char*>(::malloc(sizeof(char) * len));
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
        : m_message_id(message_id), m_totalLen(len)
    {
        m_data = static_cast<char*>(::malloc(sizeof(char) * len));
        ::memcpy(m_data, message, len);
    }

    char* m_data;
    short m_message_id;
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
}
#endif //GATESERVER_CSESSION_H
