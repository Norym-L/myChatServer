#include "AsioIOServicePool.h"
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
    auto& service = m_ioServices[m_nextIOService];
    ++m_nextIOService;
    if (m_nextIOService == m_ioServices.size())
    {
        m_nextIOService = 0;
    }
    return service;
}

void AsioIOServicePool::stop()
{
    for (const auto& work: m_works)
    {
        work->reset();
    }

    for (auto& t: m_threads)
    {
        t.join();
    }
}
