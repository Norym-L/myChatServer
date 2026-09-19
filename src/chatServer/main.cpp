#include <csignal>
#include <thread>
#include <mutex>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>

#include "Cserver.h"
#include "LogicSystem.h"
#include "AsioIOServicePool.h"

bool b_stop = false;
std::condition_variable condition_variable;
std::mutex mutex_quit;

int main()
{
    try
    {
        auto& config = ConfigManager::GetInstance();
        auto& pool = AsioIOServicePool::GetInstance();
        boost::asio::io_context io_context;
        boost::asio::signal_set signal(io_context, SIGINT, SIGTERM);
        signal.async_wait([&io_context, &pool](auto, auto)
            {
            io_context.stop();
            pool.stop();
            });
        auto port_str = config["SelfServer"]["port"];
        CServer server(io_context, atoi(port_str.c_str()));
        io_context.run();
    }
    catch (std::exception& e)
    {
        spdlog::error("[chat server] start error: {}", e.what());
    }
}
