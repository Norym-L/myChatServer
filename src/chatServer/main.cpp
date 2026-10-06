#include <csignal>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>

#include "CServer.h"
#include "LogicSystem.h"
#include <AsioIOServicePool.h>

namespace
{
/// 单实例部署：一个进程只运行一个 ChatServer 实例，跑哪一个由 --config 指向的配置文件决定。
/// 约定该配置文件中恰好存在一个 [ChatServerN] 段，段名即本实例的身份
std::string resolveSelfSection(ConfigManager &config)
{
    constexpr std::string_view prefix("ChatServer");

    std::vector<std::string> matched;
    for (const auto &name: config.sectionNames())
    {
        if (name.starts_with(prefix))
        {
            matched.emplace_back(name);
        }
    }

    if (matched.size() != 1)
    {
        throw std::runtime_error("config file must contain exactly one [" + std::string(prefix) +
                                 "N] section, but found " + std::to_string(matched.size()));
    }

    return matched.front();
}
}

int main(int argc, char *argv[])
{
    try
    {
        // 必须在首次 GetInstance() 之前解析，单例构造时就会读配置文件
        ConfigManager::initFromArgs(argc, argv);
        auto &config = ConfigManager::GetInstance();

        const auto self_section = resolveSelfSection(config);

        const auto name = config[self_section]["name"];
        const auto port_str = config[self_section]["port"];
        if (name.empty() || port_str.empty())
        {
            throw std::runtime_error("config section [" + self_section +
                                     "] is incomplete: port/name are required");
        }

        // 端口必须与 StatusServer 向客户端分发的地址一致，否则客户端拿到的端口连不上
        const auto port = static_cast<std::uint16_t>(std::stoi(port_str));

        auto &pool = AsioIOServicePool::GetInstance();
        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, &pool](auto, auto)
        {
            io_context.stop();
            pool.stop();
        });

        spdlog::info("[{}] starting as {}, listen on port: {}", self_section, name, port);
        CServer server(io_context, port, name);
        io_context.run();
    }
    catch (const std::exception &e)
    {
        spdlog::error("Chat Server start error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
