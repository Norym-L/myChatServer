#include <iostream>

#include "../public/ConfigManager.h"
#include "httpServer/CServer.h"
#include "../public/const.h"

#include <spdlog/spdlog.h>

#include <RedisManager.h>
#include <sql/mysql/MySqlManager.h>
#include "statusClient/StatusGrpcClient.h"
#include "verifyClient/VerifyGrpcClient.h"

int main(int argc, char *argv[])
{
    try
    {
        // 必须在首次 GetInstance() 之前解析，单例构造时就会读配置文件
        ConfigManager::initFromArgs(argc, argv);

        // 日志级别同样来自配置文件，必须放在 try 内：配置缺失或键缺失会抛异常，
        // 落在 try 外将直接 std::terminate 且没有任何诊断输出
        spdlog::default_logger()->set_level(static_cast<spdlog::level::level_enum>(std::stoi(ConfigManager::GetInstance()["LoggerLevel"]["default"])));
        const auto gate_port_str = ConfigManager::GetInstance()["GateServer"]["port"];
        const auto port = static_cast<unsigned short>(std::stoi(gate_port_str));
        net::io_context ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code &error, int signal_number)
        {
            if (error)
            {
                spdlog::error("error at signals cache");
                return;
            }
            ioc.stop();
        });

        // 预热一次性依赖：MySQL 连接池在构造时同步建立多条 SSL 连接（冷启动可达数秒），
        // Redis 池与 gRPC 通道同理。若不预热，这些开销会落在首个请求的处理路径上，
        // 既拉长首请求耗时，也让它逼近连接超时窗口
        spdlog::info("Gate Server warming up dependencies...");
        ConfigManager::GetInstance(); // 配置已在上文读取，此处保证初始化顺序清晰
        MySqlManager::GetInstance();
        // 协程版 Redis：自带 io_context + 单线程，HTTP 工作线程上的协程会自动切到该线程执行命令
        RedisManager::GetInstance().init();
        StatusGrpcClient::GetInstance();
        VerifyGrpcClient::GetInstance();
        spdlog::info("Gate Server dependencies ready");

        std::make_shared<CServer>(ioc, port)->start();
        spdlog::info("Gate Server start and listen on port: {}", port);
        ioc.run();
    }
    catch (std::exception &exp)
    {
        spdlog::error("Gate Server start error: {}", exp.what());
        return EXIT_FAILURE;
    }
}
