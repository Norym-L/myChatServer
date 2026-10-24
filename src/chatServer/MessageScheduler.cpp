#include "MessageScheduler.h"

#include "ConfigManager.h"
#include "CServer.h"
#include <MessageType.h>

#include "data/UserManager.h"

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
            co_await streamCreateGroup(stream_name, m_groupName, -1);
            for (;;)
            {
                const auto stream_results = co_await streamReadGroup(m_groupName, m_groupConsumer, stream_name, MessageReadMode::New, 100, 500);
                if (stream_results.empty())
                {
                    continue;
                }
                for (const auto& [id, fields]: stream_results.front().entries)
                {
                    chat_message::Message message;
                    message.message_id = id;
                    message.group_checked = true;
                    for (const auto& [key, value]: fields)
                    {
                        if (key == "group_id") message.group_id = std::stoull(value);
                        else if (key == "from_uid") message.from_uid = std::stoull(value);
                        else if (key == "to_uid") message.to_uid = std::stoull(value);
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
        catch (const std::exception& e)
        {
            SPDLOG_WARN("[message-receiver] broken, restart now error: {}", e.what());
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
        try
        {
            for (;;)
            {
                auto [ec, message] = co_await m_channel->async_receive(net::as_tuple);
                if (ec) co_return;
                if (message.message_id.empty()) // 解析层已经坏了，这条只能丢
                {
                    SPDLOG_WARN("[message-sender] empty stream id, drop message from {}", message.from_uid);
                    continue;
                }
                try
                {
                    co_await streamAckDel(ONLINE_MESSAGE_STREAM + m_serverName, m_groupName, {message.message_id}); // 确认删除在线流
                }
                catch (const std::exception& e)
                {
                    SPDLOG_WARN("ackdel failed, id: {}, error: {}", message.message_id, e.what());
                }
                const auto session = UserManager::GetInstance().getSession(message.to_uid);
                if (!session ||
                    !co_await net::co_spawn(session->getSocket().get_executor(),
                                            session->sendMessage(message),
                                            net::use_awaitable))
                {
                    co_await insertMessageToOffline(std::move(message));
                }
            }
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("[message-sender] broken, restart now error: {}", e.what());
        }
    }
}
}

void MessageScheduler::init(CServer* cserver, net::io_context& io_context)
{
    const auto server_name = cserver->getSelfServerName();
    const auto channel = std::make_shared<MessageChannel>(io_context, 1000);
    m_receiver.start(io_context, channel, server_name);
    m_sender.start(cserver, io_context, channel, server_name);
}
