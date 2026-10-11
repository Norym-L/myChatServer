#include <RedisManager.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>
#include <json/reader.h>

#include "Defer.h"

RedisConnectPool::RedisConnectPool(const size_t pool_size, std::string host, const int port, const std::string& password)
    : m_poolSize(pool_size), m_host(std::move(host)), m_port(port), m_password(password), m_beStop(false)
{
    for (size_t i = 0; i < m_poolSize; ++i)
    {
        // 连接与 AUTH 逻辑抽到 createConnection()，供坏连接重建复用；密码保存为成员供重建时使用
        auto* context = createConnection();
        if (context == nullptr)
        {
            continue;
        }

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

redisContext* RedisConnectPool::createConnection() const
{
    // 建连逻辑（connect + AUTH），构造函数与坏连接重建共用
    auto* context = redisConnect(m_host.c_str(), m_port);
    if (context == nullptr || context->err != 0)
    {
        if (context != nullptr)
        {
            redisFree(context);
        }
        return nullptr;
    }

    const auto reply = static_cast<redisReply*>(redisCommand(context, "AUTH %s", m_password.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        spdlog::error("[RedisManager] redis password not true");
        freeReplyObject(reply);
        redisFree(context);
        return nullptr;
    }

    freeReplyObject(reply);
    return context;
}

void RedisConnectPool::recycleBadConnection(redisContext* context)
{
    // 坏连接不回池：销毁后尝试重建新连接补充池（重建失败则池容量减少一个）
    redisFree(context);
    if (auto* new_context = createConnection(); new_context != nullptr)
    {
        returnConnection(new_context);
    }
}

redisContext* RedisConnectPool::getConnection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // 限时等待：池为空（如 Redis 不可达导致初始连接全部失败）时不允许永久阻塞调用线程，超时返回空由调用方判空
    if (!m_condition.wait_for(lock, std::chrono::seconds(2), [this]()
                                  {
                                      if (m_beStop)
                                      {
                                          return true;
                                      }

                                      return !m_connections.empty();
                                  }))
    {
        return nullptr;
    }
    if (m_beStop)
    {
        return nullptr;
    }
    auto* connect = m_connections.front();
    m_connections.pop();
    return connect;
}

void RedisConnectPool::returnConnection(redisContext* context)
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
    auto& config = ConfigManager::GetInstance();

    // [Redis] 段缺失时 std::stoi("") 抛出的 invalid_argument 完全看不出缺的是哪个配置项；
    // 而且构造函数抛异常会让 GetInstance() 每次调用都重试一次构造，
    // 伴随 TSingLeton 基类析构日志（[Singleton] singleton destruct）反复刷屏。
    // 这里先显式校验并把配置项名与当前配置文件路径写进错误信息。
    const auto host = config["Redis"]["host"];
    const auto port_str = config["Redis"]["port"];
    const auto passwd = config["Redis"]["passwd"];
    if (host.empty() || port_str.empty() || passwd.empty())
    {
        throw std::runtime_error("[RedisManager] config section [Redis] is incomplete, "
                                 "host/port/passwd are required in " + ConfigManager::configPath());
    }

    m_connectPool = std::make_unique < RedisConnectPool > (
                        5,
                        host,
                        std::stoi(port_str),
                        passwd
                    );
}

RedisManager::~RedisManager()
{
    close();
}

void RedisManager::insert(const std::shared_ptr<UserInfo>& user_info) const
{
    if (!user_info)
    {
        return;
    }

    Json::Value redis_root;
    redis_root["token"] = user_info->token.empty() ? "" : user_info->token;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["desc"] = user_info->desc;
    redis_root["icon"] = user_info->icon;
    redis_root["uid"] = user_info->uid;
    redis_root["sex"] = user_info->sex;
    if (!set(USER_BASE_INFO + std::to_string(user_info->uid), redis_root.toStyledString()))
    {
        spdlog::warn("[redis] redis insert user_info failed!");
    }
}

void RedisManager::insert(const std::vector<std::shared_ptr<UserInfo>>& user_infos) const
{
    // 按用户名查不到用户时调用方会传入空列表：此时不应写缓存，
    // 更不能对空列表取 front()（Debug 构建下直接断言崩溃，Release 下是未定义行为）
    if (user_infos.empty() || !user_infos.front())
    {
        return;
    }

    Json::Value redis_root;
    for (const auto& user_info : user_infos)
    {
        if (!user_info)
        {
            continue;
        }
        redis_root["uids"].append(user_info->uid);
    }
    if (!set(USER_BASE_INFO_BY_NAME + user_infos.front()->name, redis_root.toStyledString()))
    {
        spdlog::warn("[redis] redis insert user_info failed!");
    }
}

bool RedisManager::get(const std::string& key, std::string& value) const
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
            m_connectPool->returnConnection(connect);
        }
        else
        {
            // reply 为空说明连接已断开：坏连接不回池并尝试重建
            m_connectPool->recycleBadConnection(connect);
        }
        return false;
    }

    value = reply->str;
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    spdlog::debug("[RedisManager] redis Succeed to execute comand [ GET {} ]", key);
    return true;
}

bool RedisManager::set(const std::string& key, const std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "SET %s %s", key.c_str(), value.c_str()));

    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ SET {} {} ]", key, value);
        // reply 为空说明连接已断开：坏连接不回池并尝试重建
        m_connectPool->recycleBadConnection(connect);
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

bool RedisManager::lPush(const std::string& key, const std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str()));
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

bool RedisManager::lPop(const std::string& key, std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "LPOP %s", key.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ LPOP {} ]", key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
            m_connectPool->returnConnection(connect);
        }
        else
        {
            // reply 为空说明连接已断开：坏连接不回池并尝试重建
            m_connectPool->recycleBadConnection(connect);
        }
        return false;
    }
    value = reply->str;
    spdlog::debug("[RedisManager] redis Succeed to execut command [ LPOP {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::rPush(const std::string& key, const std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ RPUSH {} {} ]", key, value);
        // reply 为空说明连接已断开：坏连接不回池并尝试重建
        m_connectPool->recycleBadConnection(connect);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ RPUSH {} {} ]", key, value);
        freeReplyObject(reply);
        m_connectPool->returnConnection(connect);
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ RPUSH {} {} ]", key, value);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::rPop(const std::string& key, std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "RPOP %s", key.c_str()));
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

bool RedisManager::hSet(const std::string& key, const std::string& h_key, const std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "HSET %s %s %s", key.c_str(), h_key.c_str(), value.c_str()));
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

bool RedisManager::hSet(const char* key, const char* h_key, const char* h_value, const std::size_t h_value_len) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const char* argv[4];
    std::size_t argv_len[4];
    argv[0] = "HSET";
    argv_len[0] = 4;
    argv[1] = key;
    argv_len[1] = strlen(key);
    argv[2] = h_key;
    argv_len[2] = strlen(h_key);
    argv[3] = h_value;
    argv_len[3] = h_value_len;

    const auto reply = static_cast<redisReply*>(redisCommandArgv(connect, 4, argv, argv_len));
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

bool RedisManager::hDel(const std::string& key, const std::string& field) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    Defer defer([&connect, this]()
        {
            m_connectPool->returnConnection(connect);
        });

    auto* reply = static_cast<redisReply*>(redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ HDEL {} {} ]", key, field);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
        }
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ HDEL {} {} ]", key, field);
    freeReplyObject(reply);
    return true;
}

std::string RedisManager::hGet(const std::string& key, const std::string& h_key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return "";
    }

    const char* argv[3];
    std::size_t argv_len[3];
    argv[0] = "HGET";
    argv_len[0] = 4;
    argv[1] = key.c_str();
    argv_len[1] = key.length();
    argv[2] = h_key.c_str();
    argv_len[2] = h_key.length();

    const auto reply = static_cast<redisReply*>(redisCommandArgv(connect, 3, argv, argv_len));
    if (reply == nullptr || reply->type == REDIS_REPLY_NIL)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ HGET {} {} ]", key, h_key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
            m_connectPool->returnConnection(connect);
        }
        else
        {
            // reply 为空说明连接已断开：坏连接不回池并尝试重建
            m_connectPool->recycleBadConnection(connect);
        }
        return "";
    }

    const std::string value = reply->str;
    spdlog::debug("[RedisManager] redis Succeed to Execut command [ HGET {} {} ]", key, h_key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return value;
}

bool RedisManager::del(const std::string& key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply*>(redisCommand(connect, "DEL %s", key.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        spdlog::debug("[RedisManager] redis Failed to Execut command [ DEL {} ]", key);
        if (reply != nullptr)
        {
            freeReplyObject(reply);
            m_connectPool->returnConnection(connect);
        }
        else
        {
            // reply 为空说明连接已断开：坏连接不回池并尝试重建
            m_connectPool->recycleBadConnection(connect);
        }
        return false;
    }

    spdlog::debug("[RedisManager] redis Succeed to execut command [ DEL {} ]", key);
    m_connectPool->returnConnection(connect);
    freeReplyObject(reply);
    return true;
}

bool RedisManager::existsKey(const std::string& key) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply*>(redisCommand(connect, "exists %s", key.c_str()));
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
