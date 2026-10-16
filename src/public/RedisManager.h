#ifndef GATESERVER_REDISMANAGER_H
#define GATESERVER_REDISMANAGER_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <hiredis/hiredis.h>

#include <const.h>

#include "Singleton.h"
#include "data/UserInfo.h"

/// redis连接池
class RedisConnectPool
{
public:
    RedisConnectPool(size_t pool_size, std::string host, int port, const std::string& password);
    ~RedisConnectPool();

    redisContext* getConnection();
    void returnConnection(redisContext* context);
    /// 坏连接不回池：销毁后尝试重建新连接补充池（供 RedisManager 各命令方法在连接断开时调用）
    void recycleBadConnection(redisContext* context);
    void close();

private:
    std::size_t m_poolSize;
    std::string m_host;
    int m_port;
    std::string m_password; // 保存密码，供坏连接重建时 AUTH 使用
    std::atomic<bool> m_beStop;
    std::queue<redisContext*> m_connections;
    std::mutex m_mutex;
    std::condition_variable m_condition;

    /// 创建并 AUTH 一个新连接（连接/AUTH 失败返回 nullptr）
    redisContext* createConnection() const;
};

class RedisManager : public TSingLeton<RedisManager>
{
    friend class TSingLeton;

public:
    ~RedisManager();

    void insert(const std::shared_ptr<UserInfo>& user_info, long ex = 500) const;
    void insert(const std::vector<std::shared_ptr<UserInfo> >& user_infos, long ex = 500) const;
    /// 获取用户ip，用户不在线返回空字符串
    [[nodiscard]] std::string isUserOnLine(std::uint64_t uid) const;

    [[nodiscard]] bool get(const std::string& key, std::string& value) const;
    [[nodiscard]] bool set(const std::string& key, const std::string& value, long ex = 30) const;
    [[nodiscard]] bool lPush(const std::string& key, const std::string& value) const;
    [[nodiscard]] bool lPop(const std::string& key, std::string& value) const;
    [[nodiscard]] bool rPush(const std::string& key, const std::string& value) const;
    [[nodiscard]] bool rPop(const std::string& key, std::string& value) const;
    /// 写 hash 字段；hash 字段不支持 TTL（HSET 无 EX 语法），故不提供 ex 参数
    [[nodiscard]] bool hSet(const std::string& key, const std::string& h_key, const std::string& value) const;
    [[nodiscard]] bool hSet(const char* key, const char* h_key, const char* h_value, std::size_t h_value_len) const;
    [[nodiscard]] bool expire(const std::string& key, long extendSeconds) const;
    [[nodiscard]] bool hDel(const std::string& key, const std::string& field) const;
    [[nodiscard]] std::string hGet(const std::string& key, const std::string& h_key) const;
    [[nodiscard]] bool del(const std::string& key) const;
    [[nodiscard]] bool existsKey(const std::string& key) const;
    void close() const;

private:
    RedisManager();

    std::unique_ptr<RedisConnectPool> m_connectPool;
};


#endif //GATESERVER_REDISMANAGER_H
