#include <iostream>

#include "ConfigManager.h"
#include "httpServer/CServer.h"
#include "const.h"

#include <hiredis/hiredis.h>

#include "httpServer/RedisManager.h"

void TestRedisMgr() {
    assert(RedisManager::GetInstance().connect("127.0.0.1", 6380));
    assert(RedisManager::GetInstance().auth("123456"));
    assert(RedisManager::GetInstance().set("blogwebsite","llfc.club"));
    std::string value;
    assert(RedisManager::GetInstance().get("blogwebsite", value) );
    assert(RedisManager::GetInstance().get("nonekey", value) == false);
    assert(RedisManager::GetInstance().hSet("bloginfo","blogwebsite", "llfc.club"));
    assert(RedisManager::GetInstance().hGet("bloginfo","blogwebsite") != "");
    assert(RedisManager::GetInstance().existsKey("bloginfo"));
    assert(RedisManager::GetInstance().del("bloginfo"));
    assert(RedisManager::GetInstance().del("bloginfo"));
    assert(RedisManager::GetInstance().existsKey("bloginfo") == false);
    assert(RedisManager::GetInstance().lPush("lpushkey1", "lpushvalue1"));
    assert(RedisManager::GetInstance().lPush("lpushkey1", "lpushvalue2"));
    assert(RedisManager::GetInstance().lPush("lpushkey1", "lpushvalue3"));
    assert(RedisManager::GetInstance().rPop("lpushkey1", value));
    assert(RedisManager::GetInstance().rPop("lpushkey1", value));
    assert(RedisManager::GetInstance().lPop("lpushkey1", value));
    assert(RedisManager::GetInstance().lPop("lpushkey2", value)==false);
    RedisManager::GetInstance().close();
}

int main()
{
    TestRedisMgr();

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
