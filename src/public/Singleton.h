/**
 * @file Singleton.h
 * @brief Singleton 单例继承
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_SINGLETON_H
#define GATESERVER_SINGLETON_H
#include <iostream>
#include <spdlog/spdlog.h>

template<typename T>
class TSingLeton
{
protected:
    ~TSingLeton()
    {
        SPDLOG_DEBUG("[Singleton] singleton destruct");
    }

public:
    static T &GetInstance()
    {
        static T instance;
        return instance;
    }

    TSingLeton() = default;
    TSingLeton(const TSingLeton<T> &) = delete;
    TSingLeton &operator=(const TSingLeton<T> &st) = delete;
    TSingLeton &operator=(TSingLeton<T> &&st) = delete;
};

#endif //GATESERVER_SINGLETON_H
