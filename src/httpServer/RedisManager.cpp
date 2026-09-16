#include "RedisManager.h"

#include <memory>
#include <utility>
#include <spdlog/spdlog.h>

#include "../ConfigManager.h"

RedisConnectPool::RedisConnectPool(const size_t pool_size, std::string host, const int port, const std::string &password)
    : m_poolSize(pool_size), m_host(std::move(host)), m_port(port), m_beStop(false)
{
    for (size_t i = 0; i < m_poolSize; ++i)
    {
        auto *context = redisConnect(m_host.c_str(), m_port);
        if (context == nullptr || context->err != 0)
        {
            if (context != nullptr)
            {
                redisFree(context);
            }
            continue;
        }

        const auto reply = static_cast<redisReply *>(redisCommand(context, "AUTH %s", password.c_str()));
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
        {
            spdlog::error("[RedisManager] redis password not true");
            freeReplyObject(reply);
            redisFree(context);
            continue;
        }

        freeReplyObject(reply);
        spdlog::debug("[RedisManager] redis password true");
        m_connections.push(context);
    }
}

RedisConnectPool::~RedisConnectPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_connections.empty())
    {
        m_connections.pop();
    }
}

redisContext *RedisConnectPool::getConnection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this]()
    {
        if (m_beStop)
        {
            return true;
        }

        return !m_connections.empty();
    });
    if (m_beStop)
    {
        return nullptr;
    }
    auto *connect = m_connections.front();
    m_connections.pop();
    return connect;
}

void RedisConnectPool::returnConnection(redisContext *context)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_beStop)
    {
        return;
    }
    m_connections.push(context);
    m_condition.notify_one();
}

void RedisConnectPool::close()
{
    m_beStop = true;
    m_condition.notify_all();
}

//-----------------------------------------------------分界线-------------------------------------------

RedisManager::RedisManager()
{
    auto &config = ConfigManager::GetInstance();
    m_connectPool = std::make_unique < RedisConnectPool > (
                        5,
                        config["Redis"]["host"],
                        std::stoi(config["Redis"]["port"]),
                        config["Redis"]["passwd"]
                    );
}

RedisManager::~RedisManager()
{
    close();
}

bool RedisManager::get(const std::string &key, std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply *>(redisCommand(connect, "GET %s", key.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING)
    {
        spdlog::debug("[RedisManager] redis [GET {} ] failed", key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
        }
        m_connectPool->returnConnection(connect);
        return false;
    }

    value = reply->str;
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    spdlog::debug("[RedisManager] redis Succeed to execute comand [ GET {} ]", key);
    return true;
}

bool RedisManager::set(const std::string &key, const std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "SET %s %s", key.c_str(), value.c_str()));

    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ SET {} {} ]", key, value);
        m_connectPool->returnConnection(connect);
        return false;
    }

    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0)))
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ SET {} {} ]", key, value);
        freeReplyObject(reply);
        m_connectPool->returnConnection(connect);
        return false;
    }

    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    spdlog::debug("[RedisManager] redis Succeed to execut command [ SET {} {} ]", key, value);
    return true;
}

bool RedisManager::lPush(const std::string &key, const std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ LPUSH {} {} ]", key, value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ LPUSH {} {} ]", key, value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ LPUSH {} {} ]", key, value);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::lPop(const std::string &key, std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "LPOP %s", key.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ LPOP {} ]", key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
        }
        m_connectPool->returnConnection(connect);
        return false;
    }
    value = reply->str;
    spdlog::debug("[RedisManager] redis Succeed to execut command [ LPOP {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::rPush(const std::string &key, const std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ RPUSH {} {} ]", key, value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ RPUSH {} {} ]", key, value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ RPUSH {} {} ]", key, value);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::rPop(const std::string &key, std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "RPOP %s", key.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ RPOP {} ]", key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
        }
        m_connectPool->returnConnection(connect);
        return false;
    }
    value = reply->str;
    spdlog::debug("[RedisManager] redis Succeed to execut command [ RPOP {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::hSet(const std::string &key, const std::string &h_key, const std::string &value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply *>(redisCommand(connect, "HSET %s %s %s", key.c_str(), h_key.c_str(), value.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ HSET {} {} {} ]", key, h_key, value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to Execut command [ HSET {} {} {} ]", key, h_key, value);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::hSet(const char *key, const char *h_key, const char *h_value, const std::size_t h_value_len) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const char *argv[4];
    std::size_t argv_len[4];
    argv[0] = "HSET";
    argv_len[0] = 4;
    argv[1] = key;
    argv_len[1] = strlen(key);
    argv[2] = h_key;
    argv_len[2] = strlen(h_key);
    argv[3] = h_value;
    argv_len[3] = h_value_len;

    const auto reply = static_cast<redisReply *>(redisCommandArgv(connect, 4, argv, argv_len));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ HSET {} {} {} ]", key, h_key, h_value);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to Execut command [ HSET {} {} {} ]", key, h_key, h_value);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

std::string RedisManager::hGet(const std::string &key, const std::string &h_key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return "";
    }

    const char *argv[3];
    std::size_t argv_len[3];
    argv[0] = "HGET";
    argv_len[0] = 4;
    argv[1] = key.c_str();
    argv_len[1] = key.length();
    argv[2] = h_key.c_str();
    argv_len[2] = h_key.length();

    const auto reply = static_cast<redisReply *>(redisCommandArgv(connect, 3, argv, argv_len));
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ HGET {} {} ]", key, h_key);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return "";
    }

    const std::string value = reply->str;
    spdlog::debug("[RedisManager] redis Succeed to Execut command [ HGET {} {} ]", key, h_key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return value;
}

bool RedisManager::del(const std::string &key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply *>(redisCommand(connect, "DEL %s", key.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ DEL {} ]", key);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ DEL {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::existsKey(const std::string &key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply *>(redisCommand(connect, "exists %s", key.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER || reply->integer == 0)
    {
        spdlog::debug("[RedisManager] redis Not Found [ KEY {} ]", key);
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    spdlog::debug("[RedisManager] redis Found [ KEY {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

void RedisManager::close() const
{
    m_connectPool->close();
}
