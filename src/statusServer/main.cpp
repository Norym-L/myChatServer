#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <boost/asio.hpp>

#include "grpc/StatusServerImpl.h"
#include <ConfigManager.h>

static void RunServer()
{
    auto &config = ConfigManager::GetInstance();
    const auto server_list = config["ChatServer"]["servers"];

    std::vector<std::string> chat_servers{};
    {
        std::stringstream stringstream(server_list);
        std::string chat_server;

        while (std::getline(stringstream, chat_server, ','))
        {
            chat_servers.push_back(chat_server);
        }
    }

    // 配置完整性校验：聊天服务器配置缺失会让 StatusServer 向客户端分发空地址，启动即报错更易定位
    for (const auto& section: chat_servers)
    {
        if (config[section]["host"].empty() ||
            config[section]["port"].empty() ||
            config[section]["name"].empty())
        {
            throw std::runtime_error(std::string("config section [") + section +
                                     "] is incomplete: host/port/name are required");
        }
    }

    std::string server_address(config["StatusServer"]["host"] + ":" + config["StatusServer"]["port"]);
    StatusServerImpl server_impl;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&server_impl);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    // BuildAndStart 失败时返回空指针，继续调用 Wait() 会空指针崩溃
    if (server == nullptr)
    {
        spdlog::error("Status Server failed to build and start on {}", server_address);
        return;
    }
    spdlog::info("Status Server listening on {}", server_address);

    boost::asio::io_context io_context;
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

    signals.async_wait([&server](const boost::system::error_code &error, int signal_number)
    {
        if (!error)
        {
            std::cout << "Shutting down server..." << std::endl;
            server->Shutdown();
        }
    });

    // 在单独的线程中运行io_context
    std::thread([&io_context]() { io_context.run(); }).detach();

    // 等待服务器关闭
    server->Wait();
    io_context.stop(); // 停止io_context
}

int main(int argc, char *argv[])
{
    try
    {
        // 必须在首次 GetInstance() 之前解析，单例构造时就会读配置文件
        ConfigManager::initFromArgs(argc, argv);

        // 日志级别来自配置文件，必须在 try 内读取，否则配置缺失时会直接 terminate 且无日志
        spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(std::stoi(ConfigManager::GetInstance()["LoggerLevel"]["default"])));
        RunServer();
    }
    catch (std::exception &e)
    {
        spdlog::error("Status Server start error: {}", e.what());
        return EXIT_FAILURE;
    }
}
