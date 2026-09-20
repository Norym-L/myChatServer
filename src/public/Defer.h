#ifndef GATESERVER_DEFER_H
#define GATESERVER_DEFER_H

#include <functional>

class Defer
{
public:
    template<typename F>
    explicit Defer(F&& f)
        : m_function(std::forward<F>(f)), m_cancelled(false)
    {}

    ~Defer()
    {
        if (!m_cancelled && m_function)
        {
            m_function();
        };
    }

    void cancel() noexcept
    {
        m_cancelled = true;
    }

    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;

private:
    std::function<void()> m_function;
    bool m_cancelled;
};


#endif //GATESERVER_DEFER_H
