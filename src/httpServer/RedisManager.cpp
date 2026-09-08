#include "RedisManager.h"

#include <memory>
#include <utility>

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
        if (reply->type == REDIS_REPLY_ERROR)
        {
            std::cerr << "redis passward not true \n";
            freeReplyObject(reply);
            redisFree(context);
            continue;
        }

        freeReplyObject(reply);
        std::cout << "redis passward true \n";
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
    m_connectPool = std::make_unique<RedisConnectPool>(
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
    if (reply == nullptr)
    {
        std::cerr << "redis [GET " << key << " ] failed \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        std::cerr << "redis [GET " << key << " ] failed \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    value = reply->str;
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    std::cout << "redis Succeed to execute comand [ GET " << key << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ SET " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0) || strcmp(reply->str, "ok")))
    {
        std::cerr << "redis Failed to Execut command [ SET " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    std::cout << "redis Succeed to execut command [ SET " << key << " " << value << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ LPUSH " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        std::cerr << "redis Failed to Execut command [ LPUSH " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ LPUSH " << key << " " << value << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ LPOP " << key << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    value = reply->str;
    std::cout << "redis Succeed to execut command [ LPOP " << key << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ RPUSH " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        std::cerr << "redis Failed to Execut command [ RPUSH " << key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ RPUSH " << key << " " << value << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ RPOP " << key << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }
    value = reply->str;
    std::cout << "redis Succeed to execut command [ RPOP " << key << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ HSET " << key << " " << h_key << " " << value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Succeed to Execut command [ HSET " << key << " " << h_key << " " << value << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ HSET " << key << " " << h_key << " " << h_value << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Succeed to Execut command [ HSET " << key << " " << h_key << " " << h_value << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ HGET " << key << " " << h_key << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return "";
    }

    const std::string value = reply->str;
    std::cout << "redis Succeed to Execut command [ HGET " << key << " " << h_key << " ] \n";
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
        std::cerr << "redis Failed to Execut command [ DEL " << key << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ DEL " << key << " ] \n";
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
        std::cerr << "redis Not Found [ KEY " << key << " ] \n";
        m_connectPool->returnConnection(connect);
        freeReplyObject(reply);
        return false;
    }

    std::cout << "redis Found [ KEY " << key << " ] \n";
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

void RedisManager::close() const
{
    m_connectPool->close();
}
