#include "RedisManager.h"

RedisManager::RedisManager()
    : m_connect(nullptr), m_reply(nullptr)
{}

RedisManager::~RedisManager()
{
    close();
}

bool RedisManager::connect(const std::string &host, const int port)
{
    m_connect = redisConnect(host.c_str(), port);
    if (m_connect == nullptr)
    {
        std::cerr << "redis connect error: the redis context init failed \n";
        return false;
    }
    if (m_connect->err)
    {
        std::cerr << "redis connect error: " << m_connect->errstr << "\n";
        return false;
    }
    return true;
}

bool RedisManager::get(const std::string &key, std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "GET %s", key.c_str()));
    if (m_reply == nullptr)
    {
        std::cerr << "redis [GET " << key << " ] failed \n";
        freeReplyObject(m_reply);
        return false;
    }
    if (m_reply->type != REDIS_REPLY_STRING)
    {
        std::cerr << "redis [GET " << key << " ] failed \n";
        freeReplyObject(m_reply);
        return false;
    }

    value = m_reply->str;
    freeReplyObject(m_reply);
    std::cout << "redis Succeed to execute comand [ GET " << key << " ] \n";
    return true;
}

bool RedisManager::set(const std::string &key, const std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "SET %s %s", key.c_str(), value.c_str()));

    if (m_reply == nullptr)
    {
        std::cerr << "redis Failed to Execut command [ SET " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    if (!(m_reply->type == REDIS_REPLY_STATUS && (strcmp(m_reply->str, "OK") == 0) || strcmp(m_reply->str, "ok")))
    {
        std::cerr << "redis Failed to Execut command [ SET " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    freeReplyObject(m_reply);
    std::cout << "redis Succeed to execut command [ SET " << key << " " << value << " ] \n";
    return true;
}

bool RedisManager::auth(const std::string &password)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "AUTH %s", password.c_str()));
    if (m_reply->type == REDIS_REPLY_ERROR)
    {
        std::cout << "redis Failed to Auth \n";
        freeReplyObject(m_reply);
        return false;
    }

    freeReplyObject(m_reply);
    std::cout << "redis Succeed to Auth \n";
    return true;
}

bool RedisManager::lPush(const std::string &key, const std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "LPUSH %s %s", key.c_str(), value.c_str()));
    if (m_reply == nullptr)
    {
        std::cerr << "redis Failed to Execut command [ LPUSH " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }
    if (m_reply->type != REDIS_REPLY_INTEGER || m_reply->integer <= 0)
    {
        std::cerr << "redis Failed to Execut command [ LPUSH " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ LPUSH " << key << " " << value << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::lPop(const std::string &key, std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "LPOP %s", key.c_str()));
    if (m_reply == nullptr || m_reply->type == REDIS_REPLY_NIL)
    {
        std::cerr << "redis Failed to Execut command [ LPOP " << key << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }
    value = m_reply->str;
    std::cout << "redis Succeed to execut command [ LPOP " << key << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::rPush(const std::string &key, const std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "RPUSH %s %s", key.c_str(), value.c_str()));
    if (m_reply == nullptr)
    {
        std::cerr << "redis Failed to Execut command [ RPUSH " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }
    if (m_reply->type != REDIS_REPLY_INTEGER || m_reply->integer <= 0)
    {
        std::cerr << "redis Failed to Execut command [ RPUSH " << key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ RPUSH " << key << " " << value << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::rPop(const std::string &key, std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "RPOP %s", key.c_str()));
    if (m_reply == nullptr || m_reply->type == REDIS_REPLY_NIL)
    {
        std::cerr << "redis Failed to Execut command [ RPOP " << key << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }
    value = m_reply->str;
    std::cout << "redis Succeed to execut command [ RPOP " << key << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::hSet(const std::string &key, const std::string &h_key, const std::string &value)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "HSET %s %s %s", key.c_str(), h_key.c_str(), value.c_str()));
    if (m_reply == nullptr || m_reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr << "redis Failed to Execut command [ HSET " << key << " " << h_key << " " << value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Succeed to Execut command [ HSET " << key << " " << h_key << " " << value << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::hSet(const char *key, const char *h_key, const char *h_value, const std::size_t h_value_len)
{
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

    m_reply = static_cast<redisReply *>(redisCommandArgv(m_connect, 4, argv, argv_len));
    if (m_reply == nullptr || m_reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr << "redis Failed to Execut command [ HSET " << key << " " << h_key << " " << h_value << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Succeed to Execut command [ HSET " << key << " " << h_key << " " << h_value << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

std::string RedisManager::hGet(const std::string &key, const std::string &h_key)
{
    const char *argv[3];
    std::size_t argv_len[3];
    argv[0] = "HSET";
    argv_len[0] = 4;
    argv[1] = key.c_str();
    argv_len[1] = key.length();
    argv[2] = h_key.c_str();
    argv_len[2] = h_key.length();

    m_reply = static_cast<redisReply *>(redisCommandArgv(m_connect, 3, argv, argv_len));
    if (m_reply == nullptr || m_reply->type == REDIS_REPLY_NIL)
    {
        std::cerr << "redis Failed to Execut command [ HGET " << key << " " << h_key << " ] \n";
        freeReplyObject(m_reply);
        return "";
    }

    const std::string value = m_reply->str;
    std::cout << "redis Succeed to Execut command [ HGET " << key << " " << h_key << " ] \n";
    freeReplyObject(m_reply);
    return value;
}

bool RedisManager::del(const std::string &key)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "DEL %s", key.c_str()));
    if (m_reply == nullptr || m_reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr << "redis Failed to Execut command [ DEL " << key << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Succeed to execut command [ DEL " << key << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

bool RedisManager::existsKey(const std::string &key)
{
    m_reply = static_cast<redisReply *>(redisCommand(m_connect, "exists %s", key.c_str()));
    if (m_reply == nullptr || m_reply->type != REDIS_REPLY_INTEGER || m_reply->integer == 0)
    {
        std::cerr << "redis Not Found [ KEY " << key << " ] \n";
        freeReplyObject(m_reply);
        return false;
    }

    std::cout << "redis Found [ KEY " << key << " ] \n";
    freeReplyObject(m_reply);
    return true;
}

void RedisManager::close() const
{
    redisFree(m_connect);
}
