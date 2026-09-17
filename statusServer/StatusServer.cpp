#include <grpcpp/server_builder.h>
#include <spdlog/spdlog.h>
#include <string>

#include "StatusServerImpl.h"
#include "../src/ConfigManager.h"

static void RunServer()
{
    auto &config = ConfigManager::GetInstance();
    std::string server_address(config["StatusServerForServer"]["host"] + ":" + config["StatusServerForServer"]["port"]);
    StatusServerImpl server_impl;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&server_impl);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
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
    spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(std::stoi(ConfigManager::GetInstance()["LoggerLevel"]["default"])));
    try
    {
        RunServer();
    }
    catch (std::exception &e)
    {
        spdlog::error("Status Server start error: {}", e.what());
        return EXIT_FAILURE;
    }
}
