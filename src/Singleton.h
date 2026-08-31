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

template <typename T>
class TSingLeton
{
protected:
    TSingLeton() = default;
    TSingLeton(const TSingLeton<T>&) = delete;
    TSingLeton& operator=(const TSingLeton<T>& st) = delete;

    ~TSingLeton()
    {
        std::cout << "singleton destruct" << std::endl;
    }

public:
    static T& GetInstance()
    {
        static T instance;
        return instance;
    }
};

#endif //GATESERVER_SINGLETON_H
