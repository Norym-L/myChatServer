#ifndef GATESERVER_ASIOIOSERVICEPOOL_H
#define GATESERVER_ASIOIOSERVICEPOOL_H

#include <vector>
#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>

#include <../public/Singleton.h>

namespace chat
{
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
    std::atomic<std::size_t> m_nextIOService; // 两个 ChatServer 线程会并发调用 getIOServer()，索引自增需原子
    std::once_flag m_stopFlag;                // 保证 stop() 幂等，避免多个信号回调重复 join 导致 terminate
};
}
#endif //GATESERVER_ASIOIOSERVICEPOOL_H
