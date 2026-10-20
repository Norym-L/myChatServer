#ifndef GATESERVER_CSESSION_H
#define GATESERVER_CSESSION_H

#include <string>
#include <boost/asio.hpp>
#include <mutex>
#include <queue>

#include <const.h>

using boost::asio::ip::tcp;

namespace net = boost::asio;

#define MAX_LENGTH      65535
#define MAX_ID_NUMBER   22222
#define HEAD_TOTAL_LEN  4 // tcp头部长度
#define HEAD_ID_LEN     2 // tcp头部 message_id 长度
#define HEAD_DATA_LEN   2 // tcp头部 message_len 长度
#define MAX_SEND_QUEUE_LEN 4 // tcp发送队列最大长度


class CServer;
class ReceiveNode;
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

    net::awaitable<void> readLoop();

    void send(const char* message, size_t len, short message_id);
    void handleWrite(const boost::system::error_code& error_code, std::shared_ptr<CSession> shared_self);

    /**
     * 给当前session绑定用户发送好友验证申请（接收到好友请求）
     * @param add_friend_request_id 好友请求编号
     */
    void sendAuthFriendRequest(std::uint64_t add_friend_request_id);

    /**
     * 给当前session绑定用户发送好友验证申请的结果
     * @param add_friend_request_id 好友请求编号
     * @param state 状态 1接受 2拒绝
     */
    void sendFriendRequestResult(std::uint64_t add_friend_request_id, int state);

    void notifyReceiveMessage();

    void close();

private:
    char m_headData[HEAD_TOTAL_LEN];
    tcp::socket m_socket;
    CServer* m_server;
    std::string m_sessionId;
    std::mutex m_sendMutex;
    std::queue<std::shared_ptr<SendNode> > m_sendQueue;
    std::uint64_t m_userId{0};
};


class ReceiveNode
{
public:
    ~ReceiveNode()
    {
        ::free(data);
    }

    explicit ReceiveNode(const std::uint16_t len, const short id)
        : messageId(id), totalLen(len)
    {
        // 显示使用 len，不靠结尾'\0'
        data = static_cast<char*>(::malloc(sizeof(char) * len));
    }

    char* data;
    short messageId;
    std::uint16_t currentLen = 0;
    std::uint16_t totalLen = MAX_LENGTH;
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
public:
    LogicNode(const std::shared_ptr<CSession>& session, const std::shared_ptr<ReceiveNode>& receive_node)
        : session(session), receviceNode(receive_node)
    {}

    std::shared_ptr<CSession> session;
    std::shared_ptr<ReceiveNode> receviceNode;
};
#endif //GATESERVER_CSESSION_H
