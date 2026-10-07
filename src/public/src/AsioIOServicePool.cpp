#include <AsioIOServicePool.h>
#include <spdlog/spdlog.h>

AsioIOServicePool::AsioIOServicePool(const std::size_t size)
    : m_ioServices(size),
      m_works(size),
      m_nextIOService(0)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        // 监听m_ioServices[i].run()，使其不退出
        m_works[i] = std::make_unique<WorkGuard>(m_ioServices[i].get_executor());
    }

    // 循环遍历ioService 启动多个线程，每个线程启动ioService
    for (std::size_t i = 0; i < m_ioServices.size(); ++i)
    {
        m_threads.emplace_back([this, i]()
            {
                m_ioServices[i].run();
            });
    }
}

AsioIOServicePool::~AsioIOServicePool()
{
    stop();
    spdlog::debug("[AsioIOServicePool] AsioIOServicePool destruct");
}

boost::asio::io_context& AsioIOServicePool::getIOServer()
{
    // 两个 ChatServer 线程会并发调用本方法：索引自增必须原子，否则轮询索引存在数据竞争
    const std::size_t index = m_nextIOService.fetch_add(1, std::memory_order_relaxed) % m_ioServices.size();
    return m_ioServices[index];
}

void AsioIOServicePool::stop()
{
    // 可能被多个线程的信号回调调用：用 call_once 保证清理只执行一次，避免重复 join
    std::call_once(
        m_stopFlag,
        [this]()
            {
                for (const auto& work: m_works)
                {
                    work->reset();
                }

                for (auto& t: m_threads)
                {
                    t.join();
                }
            });
}
