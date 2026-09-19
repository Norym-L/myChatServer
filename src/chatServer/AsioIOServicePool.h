#ifndef GATESERVER_ASIOIOSERVICEPOOL_H
#define GATESERVER_ASIOIOSERVICEPOOL_H

#include <vector>
#include <boost/asio.hpp>
#include <memory>

#include <Singleton.h>

class AsioIOServicePool : public TSingLeton<AsioIOServicePool>
{
    friend class TSingLeton;

public:
    using IOService = boost::asio::io_context;
    using WorkGuard = boost::asio::executor_work_guard<IOService::executor_type>;
    using WorkPtr = std::unique_ptr<WorkGuard>;

    ~AsioIOServicePool();
    AsioIOServicePool(const AsioIOServicePool&) = delete;
    AsioIOServicePool& operator=(const AsioIOServicePool&) = delete;
    // 使用 轮询round-robin 的方式返回一个 io_server
    boost::asio::io_context& getIOServer();
    /// 停止服务池并释放资源
    void stop();

private:
    explicit AsioIOServicePool(std::size_t size = 4);

    std::vector<IOService> m_ioServices;
    std::vector<WorkPtr> m_works;
    std::vector<std::thread> m_threads;
    std::size_t m_nextIOService;
};

#endif //GATESERVER_ASIOIOSERVICEPOOL_H
