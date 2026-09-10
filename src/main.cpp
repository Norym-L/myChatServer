#include <iostream>

#include "ConfigManager.h"
#include "httpServer/CServer.h"
#include "const.h"

#include <hiredis/hiredis.h>

#include "httpServer/RedisManager.h"

int main()
{
    const std::string gate_post_str = ConfigManager::GetInstance()["GateServer"]["port"];
    unsigned short gate_port = std::stoi(gate_post_str);

    try
    {
        auto port = static_cast<unsigned short>(8080);
        net::io_context ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code &error, int signal_number)
        {
            if (error)
            {
                return;
            }
            ioc.stop();
        });

        std::make_shared<CServer>(ioc, port)->start();
        std::cout << "Gate Server listen on port: " << port << std::endl;
        ioc.run();
    }
    catch (std::exception &exp)
    {
        std::cerr << "error: " << exp.what() << std::endl;
        return EXIT_FAILURE;
    }
}
