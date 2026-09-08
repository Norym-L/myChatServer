#ifndef GATESERVER_REDISMANAGER_H
#define GATESERVER_REDISMANAGER_H

#include <queue>
#include <hiredis/hiredis.h>

#include "../const.h"

/// redis连接池
class RedisConnectPool
{
public:
    RedisConnectPool(size_t pool_size, std::string host, int port, const std::string &password);
    ~RedisConnectPool();

    redisContext *getConnection();
    void returnConnection(redisContext *context);
    void close();

private:
    std::size_t m_poolSize;
    std::string m_host;
    int m_port;
    std::atomic<bool> m_beStop;
    std::queue<redisContext *> m_connections;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};

class RedisManager : public TSingLeton<RedisManager>
{
    friend class TSingLeton;

public:
    ~RedisManager();
    [[nodiscard]] bool get(const std::string &key, std::string &value) const;
    [[nodiscard]] bool set(const std::string &key, const std::string &value) const;
    [[nodiscard]] bool lPush(const std::string &key, const std::string &value) const;
    [[nodiscard]] bool lPop(const std::string &key, std::string &value) const;
    [[nodiscard]] bool rPush(const std::string &key, const std::string &value) const;
    [[nodiscard]] bool rPop(const std::string &key, std::string &value) const;
    [[nodiscard]] bool hSet(const std::string &key, const std::string &h_key, const std::string &value) const;
    [[nodiscard]] bool hSet(const char *key, const char *h_key, const char *h_value, std::size_t h_value_len) const;
    [[nodiscard]] std::string hGet(const std::string &key, const std::string &h_key) const;
    [[nodiscard]] bool del(const std::string &key) const;
    [[nodiscard]] bool existsKey(const std::string &key) const;
    void close() const;

private:
    RedisManager();

    std::unique_ptr<RedisConnectPool> m_connectPool;
};


#endif //GATESERVER_REDISMANAGER_H
