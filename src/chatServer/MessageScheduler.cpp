#include "MessageScheduler.h"

#include "ConfigManager.h"
#include "CServer.h"
#include <MessageType.h>

namespace detail
{
void MessageReceiver::start(net::io_context& io_context, std::shared_ptr<MessageChannel> channel, std::string server_name)
{
    m_serverName = std::move(server_name);
    init(io_context);
    m_channel = std::move(channel);
    net::co_spawn(io_context, run(), net::detached);
}

net::awaitable<void> MessageReceiver::run() const
{
    const std::string stream_name = ONLINE_MESSAGE_STREAM + m_serverName;
    for (;;)
    {
        try
        {
            for (;;)
            {
                const auto stream_results = co_await streamReadGroup(m_groupName, m_groupConsumer, stream_name, MessageReadMode::New, 100, 0);
                for (const auto& [id, fields]: stream_results.front().entries)
                {
                    chat_message::Message message;
                    message.message_id = id;
                    for (const auto& [key, value]: fields)
                    {
                        if (key == "group") value == "f" ? message.is_to_group = false : message.is_to_group = true;
                        else if (key == "group_id") message.to_uid = std::stoull(value);
                        else if (key == "from_uid") message.from_uid = std::stoull(value);
                        else if (key == "type") message.type = static_cast<chat_message::Type>(std::stoi(value));
                        else if (key == "body") message.body = value;
                    }
                    auto [ec] = co_await m_channel->async_send(boost::system::error_code{}, std::move(message), net::as_tuple);
                    if (ec == net::experimental::error::channel_closed)
                    {
                        co_return; // 关闭信号
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            SPDLOG_WARN("[message-receiver] broken, restart now");
        }
    }
}

void MessageSender::start(CServer* cserver, net::io_context& io_context, std::shared_ptr<MessageChannel> channel, std::string server_name)
{
    m_parentServer = cserver;
    m_serverName = std::move(server_name);
    init(io_context);
    m_channel = std::move(channel);
    for (size_t i = 0; i < 20; ++i)
    {
        net::co_spawn(io_context, run(), net::detached);
    }
}

net::awaitable<void> MessageSender::run()
{
    for (;;)
    {
        auto [ec, message] = co_await m_channel->async_receive(net::as_tuple);
        if (ec) co_return;
        auto message_id = message.message_id;
        co_await sendMessageIfUserOnline(std::to_string(message.to_uid), std::move(message));
        co_await insertMessageToOffline(std::move(message));
        co_await streamAckDel(ONLINE_MESSAGE_STREAM + m_serverName, "receiver", {std::move(message_id)}); // 确认删除在线流
    }
}

net::awaitable<void> MessageSender::sendMessageIfUserOnline(const std::string uid, const chat_message::Message message)
{
    getSession(uid)->sendMessage(message);
    co_return;
}


std::shared_ptr<CSession> MessageSender::getSession(const std::string& uid) const
{
    return m_parentServer->getSession(uid);
}
}

void MessageScheduler::init(CServer* cserver, net::io_context& io_context)
{
    const auto server_name = cserver->getSelfServerName();
    const auto channel = std::make_shared<MessageChannel>(io_context, 1000);
    m_receiver.start(io_context, channel, server_name);
    m_sender.start(cserver, io_context, channel, server_name);
}
