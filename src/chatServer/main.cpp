#include <csignal>
#include <thread>
#include <mutex>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>

#include "Cserver.h"
#include "LogicSystem.h"
#include "AsioIOServicePool.h"

namespace
{
bool b_stop = false;
std::condition_variable condition_variable;
std::mutex mutex_quit;
}

int main()
{
    auto runChatServer = [](const std::string& config_name)
        {
        try
        {
            auto& config = ConfigManager::GetInstance();
            auto& pool = chat::AsioIOServicePool::GetInstance();
            boost::asio::io_context io_context;
            boost::asio::signal_set signal(io_context, SIGINT, SIGTERM);
            signal.async_wait([&io_context, &pool](auto, auto)
                {
                io_context.stop();
                pool.stop();
                });
            chat::CServer server(io_context, 0, config_name);
            io_context.run();
        }
        catch (std::exception& e)
        {
            spdlog::error("[{}] start error: {}",config_name, e.what());
        }
        };

    std::thread chat_server1(runChatServer, "ChatServer1");
    std::thread chat_server2(runChatServer, "ChatServer2");
    chat_server1.join();
    chat_server2.join();
}
