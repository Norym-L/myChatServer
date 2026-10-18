#include <RedisManager.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>
#include <json/reader.h>

#include "Defer.h"

namespace detail
{
RedisConnectPool::RedisConnectPool(const size_t pool_size, std::string host, const int port, std::string password)
    : m_poolSize(pool_size), m_host(std::move(host)), m_port(port), m_password(std::move(password)), m_beStop(false)
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
}

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

    m_connectPool = std::make_unique<detail::RedisConnectPool>(
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

bool RedisManager::registUser(std::uint64_t uid, const std::string& server_name) const
{
    if (!set(USER_IP_PERFIX + std::to_string(uid), server_name) ||
        !xGroup(USER_OFFLINE_MESSAGE_STREAM + std::to_string(uid), USER_XGROUP_NAME + std::to_string(uid)))
    {
        spdlog::warn("[redis] user: {} regisert failed", uid);
        return false;
    }
    return true;
}

void RedisManager::insert(const std::shared_ptr<UserInfo>& user_info, const long ex) const
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
    if (!set(USER_BASE_INFO + std::to_string(user_info->uid), redis_root.toStyledString(), ex))
    {
        spdlog::warn("[redis] redis insert user_info failed!");
    }
}

void RedisManager::insert(const std::vector<std::shared_ptr<UserInfo> >& user_infos, const long ex) const
{
    // 按用户名查不到用户时调用方会传入空列表：此时不应写缓存，
    // 更不能对空列表取 front()（Debug 构建下直接断言崩溃，Release 下是未定义行为）
    if (user_infos.empty() || !user_infos.front())
    {
        return;
    }

    Json::Value redis_root;
    for (const auto& user_info: user_infos)
    {
        if (!user_info)
        {
            continue;
        }
        redis_root["uids"].append(user_info->uid);
    }
    if (!set(USER_BASE_INFO_BY_NAME + user_infos.front()->name, redis_root.toStyledString(), ex))
    {
        spdlog::warn("[redis] redis insert user_info failed!");
    }
}

void RedisManager::insertMessageToOffline(const chat_message::Message& message) const
{
    if (!xAdd((message.is_to_group ? GROUP_OFFLINE_MESSAGE_STREAM : USER_OFFLINE_MESSAGE_STREAM) + std::to_string(message.to_uid),
              {
                  {"from_uid", std::to_string(message.from_uid)},
                  {"type", std::to_string(message.type)},
                  {"body", message.body}
              }))
    {
        spdlog::warn("[redis] redis insert text_message failed!");
    }
}

std::vector<chat_message::Message> RedisManager::getMessageFromOffline(const std::uint64_t to_uid, std::uint64_t count) const
{
    auto* connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return {};
    }

    const std::string consumer = std::to_string(to_uid);
    const std::string group_name = USER_XGROUP_NAME + consumer;
    const std::string stream_name = USER_OFFLINE_MESSAGE_STREAM + consumer;
    const std::string count_str = std::to_string(count);

    std::vector<const char*> argv;
    std::vector<std::size_t> argvlen;

    auto push = [&](const char* s, const std::size_t n)
        {
            argv.push_back(s);
            argvlen.push_back(n);
        };

    push("XREADGROUP", 10);
    push("GROUP", 5);
    push(group_name.data(), group_name.size());
    push(consumer.data(), consumer.size());
    push("COUNT", 5);
    push(count_str.data(), count_str.size());
    push("STREAMS", 7);
    push(stream_name.data(), stream_name.size());
    push(">", 1);

    auto* reply = static_cast<redisReply*>(redisCommandArgv(connect, static_cast<int>(argv.size()), argv.data(), argvlen.data()));

    Defer defer([connect, reply, this]() noexcept
        {
            if (reply)
            {
                freeReplyObject(reply);
            }
            if (connect == nullptr)
            {
                return;
            }
            // 连接已损坏（网络错误、超时、协议错）则销毁，不污染连接池
            if (connect->err != 0)
            {
                m_connectPool->recycleBadConnection(connect);
            }
            else
            {
                m_connectPool->returnConnection(connect);
            }
        });
    if (reply == nullptr) return {};
    if (reply->type == REDIS_REPLY_ERROR || reply->type == REDIS_REPLY_NIL) return {};
    if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 1) return {}; // 无消息时 elements==0

    std::vector<chat_message::Message> messages;

    for (std::size_t s = 0; s < reply->elements; ++s)
    {
        const redisReply* stream = reply->element[s];
        if (stream == nullptr || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2)
            continue;

        // stream->element[0] = stream name, stream->element[1] = entries
        const redisReply* entries = stream->element[1];
        if (entries == nullptr || entries->type != REDIS_REPLY_ARRAY)
            continue;

        messages.reserve(messages.size() + entries->elements);

        for (std::size_t i = 0; i < entries->elements; ++i)
        {
            const redisReply* info = entries->element[i];
            if (info == nullptr || info->type != REDIS_REPLY_ARRAY || info->elements < 2)
                continue;

            const redisReply* id = info->element[0];
            const redisReply* kv = info->element[1];
            if (id == nullptr || id->type != REDIS_REPLY_STRING) continue;
            if (kv == nullptr || kv->type != REDIS_REPLY_ARRAY || kv->elements % 2 != 0) continue;

            chat_message::Message msg{};
            msg.message_id.assign(id->str ? id->str : "", id->len);

            for (std::size_t j = 0; j + 1 < kv->elements; j += 2)
            {
                const redisReply* fn = kv->element[j];
                const redisReply* fv = kv->element[j + 1];
                if (fn == nullptr || fv == nullptr ||
                    fn->type != REDIS_REPLY_STRING ||
                    fv->type != REDIS_REPLY_STRING)
                    continue;

                std::string key(fn->str ? fn->str : "", fn->len);
                std::string value(fv->str ? fv->str : "", fv->len);

                try
                {
                    if (key == "from_uid") msg.from_uid = std::stoull(value);
                    else if (key == "type") msg.type = static_cast<chat_message::Type>(std::stoi(value));
                    else if (key == "body") msg.body = std::move(value);
                }
                catch (const std::exception&)
                {
                    /* 数据损坏，跳过 */
                    continue;
                }
            }

            messages.push_back(std::move(msg));
        }
    }

    return messages;
}

std::string RedisManager::isUserOnLine(const std::uint64_t uid) const
{
    std::string ip{};
    (void) get(USER_IP_PERFIX + std::to_string(uid), ip);
    return ip;
}

bool RedisManager::xAdd(const std::string& stream_name, const std::vector<std::pair<std::string, std::string> >& field_values, const std::int64_t cut_time) const
{
    if (field_values.empty())
    {
        return false;
    }
    if (stream_name.empty())
    {
        return false;
    }
    auto* connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    std::string min_id_str;
    const bool use_minid = (cut_time > 0);

    if (use_minid)
    {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::int64_t threshold = now_ms - cut_time;

        if (threshold < 0)
        {
            threshold = 0;
        }
        min_id_str = std::to_string(threshold) + "-0";
    }
    std::vector<const char*> argv;
    std::vector<std::size_t> argvlen;
    argv.reserve(6 + 2 * field_values.size());
    argvlen.reserve(6 + 2 * field_values.size());

    auto push = [&](const char* s, std::size_t n)
        {
            argv.push_back(s);
            argvlen.push_back(n);
        };

    push("XADD", 4);
    push(stream_name.data(), stream_name.size());

    if (use_minid)
    {
        push("MINID", 5);
        push("~", 1); // 近似修剪
        push(min_id_str.data(), min_id_str.size());
    }

    push("*", 1); // 自动生成 ID

    for (const auto& [field, value]: field_values)
    {
        push(field.data(), field.size());
        push(value.data(), value.size());
    }

    auto* reply = static_cast<redisReply*>(redisCommandArgv(connect,
                                                            static_cast<int>(argv.size()),
                                                            argv.data(),
                                                            argvlen.data()));

    Defer defer([connect, reply, this]() noexcept
        {
            if (reply)
            {
                freeReplyObject(reply);
            }
            if (connect == nullptr)
            {
                return;
            }
            // 连接已损坏（网络错误、超时、协议错）则销毁，不污染连接池
            if (connect->err != 0)
            {
                m_connectPool->recycleBadConnection(connect);
            }
            else
            {
                m_connectPool->returnConnection(connect);
            }
        });

    if (reply == nullptr)
    {
        // 命令本身失败（通常 connect->err 也被设置），defer 会销毁连接
        return false;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        return false;
    }

    return reply->type == REDIS_REPLY_STRING;
}

bool RedisManager::xGroup(const std::string& stream_name, const std::string& group_name, const std::string& start_id, const bool shoold_make_stream) const
{
    auto* connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    std::vector<const char*> argv;
    std::vector<size_t> lens;

    auto push = [&](const std::string& s)
        {
            argv.push_back(s.data());
            lens.push_back(s.size());
        };

    push("XGROUP");
    push("CREATE");
    push(stream_name);
    push(group_name);
    push(start_id);
    if (shoold_make_stream)
    {
        push("MKSTREAM"); // 流不存在时自动创建
    }

    auto* reply = static_cast<redisReply*>(redisCommandArgv(connect, static_cast<int>(argv.size()), argv.data(), lens.data()));

    Defer defer([connect, reply, this]() noexcept
        {
            if (reply)
            {
                freeReplyObject(reply);
            }
            if (connect == nullptr)
            {
                return;
            }
            // 连接已损坏（网络错误、超时、协议错）则销毁，不污染连接池
            if (connect->err != 0)
            {
                m_connectPool->recycleBadConnection(connect);
            }
            else
            {
                m_connectPool->returnConnection(connect);
            }
        });
    if (!reply) return false;
    bool success = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");

    // BUSYGROUP 表示组已存在，若业务是幂等初始化，可视为成功
    if (!success && reply->type == REDIS_REPLY_ERROR)
    {
        if (std::string(reply->str).rfind("BUSYGROUP", 0) == 0)
        {
            success = true;
        }
    }

    return success;
}

bool RedisManager::get(const std::string& key, std::string& value) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const auto reply = static_cast<redisReply*>(redisCommand(connect, "GET %s", key.c_str()));
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

bool RedisManager::set(const std::string& key, const std::string& value, const long ex) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }
    const long safe_ex = (ex <= 0) ? 1 : ex; // 至少为 1 秒
    const auto reply = static_cast<redisReply*>(redisCommand(connect, "SET %s %s EX %ld", key.c_str(), value.c_str(), safe_ex));

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
    // HSET 不支持 EX（那是 SET 的语法）：此前拼出的 "HSET k f v EX n" 会被 Redis 判为语法错误而恒失败。
    // 这里复用 argv 版本：既能正确执行，也避免字段值含空格等字符时被命令拼接破坏。
    return hSet(key.c_str(), h_key.c_str(), value.c_str(), value.size());
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

// 刷新 key 的 TTL，用于心跳续期。extendSeconds 为新的存活时长
bool RedisManager::expire(const std::string& key, const long extendSeconds) const
{
    const auto connect = m_connectPool->getConnection();
    if (connect == nullptr)
    {
        return false;
    }

    const long safe_ex = (extendSeconds <= 0) ? 1 : extendSeconds;
    const auto reply = static_cast<redisReply*>(
        redisCommand(connect, "EXPIRE %s %ld", key.c_str(), safe_ex));

    if (reply == nullptr)
    {
        spdlog::debug("[RedisManager] redis Failed to execute command [ EXPIRE {} {} ]",
                      key, safe_ex);
        m_connectPool->recycleBadConnection(connect);
        return false;
    }

    // EXPIRE 返回 1=设置成功，0=key 不存在
    const bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    m_connectPool->returnConnection(connect);

    spdlog::debug("[RedisManager] redis {} to execute command [ EXPIRE {} {} ]",
                  ok ? "Succeed" : "Failed", key, safe_ex);
    return ok;
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
