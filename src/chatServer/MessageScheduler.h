#ifndef GATESERVER_SCHEDUKER_H
#define GATESERVER_SCHEDUKER_H

#include <RedisManager.h>

// 聊天消息框架
//  调度器 -> 阻塞读取 实时消息队列onlinechatmsgstream_<servername>（每个chatserver实例一个）
//  消息通过 RedisManager 发送到 接收方所在服务器的实时消息队列
//  如果用户不在线，调度器会把消息推送到用户的离线消息队列 offlinestream_<uid> （一个用户一个）
//  用户上线从这里拉取

class CServer;
class CSession;

using MessageChannel = boost::asio::experimental::channel<void(boost::system::error_code, chat_message::Message)>;

namespace detail
{
class MessageReceiver : RedisManager
{
public:
    void start(net::io_context& io_context, std::shared_ptr<MessageChannel> channel, std::string server_name);

    net::awaitable<void> run() const;

private:
    std::shared_ptr<MessageChannel> m_channel;
    std::string m_serverName;
    std::string m_groupName{"message-receiver"};
    std::string m_groupConsumer{"receiver"};
};


class MessageSender : RedisManager
{
public:
    void start(CServer* cserver, net::io_context& io_context, std::shared_ptr<MessageChannel> channel, std::string server_name);

    net::awaitable<void> run();
    net::awaitable<void> sendMessageIfUserOnline(std::string uid, chat_message::Message message);

private:
    // 可能返回无效 session
    [[nodiscard]] std::shared_ptr<CSession> getSession(const std::string& uid) const;
    CServer* m_parentServer = nullptr;
    std::shared_ptr<MessageChannel> m_channel;
    std::string m_serverName;
};
}


class MessageScheduler : public TSingLeton<MessageScheduler>
{
    friend class TSingLeton;

public:
    void init(CServer* cserver, net::io_context& io_context);

private:
    MessageScheduler() = default;
    detail::MessageReceiver m_receiver;
    detail::MessageSender m_sender;
};

#endif //GATESERVER_SCHEDUKER_H
