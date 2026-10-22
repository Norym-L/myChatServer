#include <csignal>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <spdlog/spdlog.h>
#include <grpcpp/server_builder.h>

#include <ConfigManager.h>

#include "CServer.h"
#include "LogicSystem.h"
#include <AsioIOServicePool.h>

#include "RedisManager.h"
#include "grpc/ChatServiceImpl.h"
#include "sql/mysql/MySqlManager.h"
#include "utils/config.h"
#include "utils/ServerRegistry.h"

int main(int argc, char* argv[])
{
    try
    {
        // 必须在首次 GetInstance() 之前解析，单例构造时就会读配置文件
        ConfigManager::initFromArgs(argc, argv);
        // 先构造 RedisManager，再构造 LogicSystem：静态析构顺序与之相反，保证 Redis 连接最后销毁
        RedisManager::GetInstance();
        MySqlManager::GetInstance();

        auto& config = ConfigManager::GetInstance();
        spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(std::stoi(config["LoggerLevel"]["default"])));

        const auto self_section = ResolveSelfSection(config);

        const auto name = config[self_section]["name"];
        const auto port_str = config[self_section]["port"];
        if (name.empty() || port_str.empty())
        {
            throw std::runtime_error("config section [" + self_section +
                                     "] is incomplete: port/name are required");
        }

        // 端口必须与 StatusServer 向客户端分发的地址一致，否则客户端拿到的端口连不上
        const auto port = static_cast<std::uint16_t>(std::stoi(port_str));

        auto& pool = AsioIOServicePool::GetInstance();

        std::string server_addr = config[name]["host"] + ":" + config[name]["grpcPort"];
        ChatServiceImpl grpc_serviec;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
        builder.RegisterService(&grpc_serviec);
        std::unique_ptr<grpc::Server> grpc_server(builder.BuildAndStart());
        SPDLOG_INFO("[{}<grpc_server>] listen on: {}", self_section, server_addr);

        std::thread grpc_server_thread([&grpc_server]()
            {
                grpc_server->Wait();
            });

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, &pool, &grpc_server](auto, auto)
            {
                io_context.stop();
                pool.stop();
                grpc_server->Shutdown();
            });

        // 逻辑线程的 io_context 同时承载 Redis 和 mysql 连接
        auto& logic = LogicSystem::GetInstance();
        RedisManager::GetInstance().init(logic.getIoContext());
        MySqlManager::GetInstance().init(logic.getIoContext());

        // serverheartbeat_<name> / logincount_<name> 带 30s TTL，由逻辑线程上的定时协程续期
        ServerRegistry::GetInstance().start(logic.getIoContext(),
                                            name, config[name]["host"] + ":" + config[name]["port"]);
        SPDLOG_INFO("[{}] registered to redis, heartbeat key: {}", self_section,
                    SERVER_HEARTBEAT_PREFIX + name);

        SPDLOG_INFO("[{}] starting as {}, listen on port: {}", self_section, name, port);
        CServer server(io_context, port, name);
        io_context.run();

        // 优雅退出：先摘除注册记录（存活 key / 连接数 key / 服务列表字段），再关连接池
        ServerRegistry::GetInstance().stop();
        grpc_server_thread.join();
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("Chat Server start error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
