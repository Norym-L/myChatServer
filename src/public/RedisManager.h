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

#include "MessageType.h"
#include "Singleton.h"
#include "data/UserInfo.h"

namespace detail
{
/// redis连接池
class RedisConnectPool
{
public:
    RedisConnectPool(size_t pool_size, std::string host, int port, std::string password);
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
    [[nodiscard]] redisContext* createConnection() const;
};
}

class RedisManager : public TSingLeton<RedisManager>
{
    friend class TSingLeton;

public:
    ~RedisManager();

    // 注册用户 -添加消息队列和消费组，设置用户上线
    [[nodiscard]] bool registUser(std::uint64_t uid, const std::string& server_name) const;

    void insert(const std::shared_ptr<UserInfo>& user_info, long ex = 500) const;
    void insert(const std::vector<std::shared_ptr<UserInfo> >& user_infos, long ex = 500) const;
    void insertMessageToOffline(const chat_message::Message& message) const;
    [[nodiscard]] std::vector<chat_message::Message> getMessageFromOffline(std::uint64_t to_uid, std::uint64_t count = 1) const;
    /// 获取用户ip，用户不在线返回空字符串
    [[nodiscard]] std::string isUserOnLine(std::uint64_t uid) const;

    [[nodiscard]] bool xAdd(const std::string& stream_name, const std::vector<std::pair<std::string, std::string> >& field_values, std::int64_t cut_time = 7 * 24 * 3600 * 1000) const;
    // start_id "0"从头 或 "$"最新 或具体 ID
    [[nodiscard]] bool xGroup(const std::string& stream_name, const std::string& group_name, const std::string& start_id = "0", bool shoold_make_stream = true) const;

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

    std::unique_ptr<detail::RedisConnectPool> m_connectPool;
};

#define CODE_PREFIX "code_"
#define USER_IP_PERFIX "uip_"
#define USER_TOKEN_PERFIX "utoken_"
#define IP_COUNT_PERFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define USER_BASE_INFO_BY_NAME "ubaseinfo_name_" // 需要处理多个值，连接用户uid数组
#define LOGIN_COUNT_PREFIX "logincount_" // 每实例连接数 key 前缀：logincount_<name>，EX 30s，随实例心跳续期

#define HEART_BEAT_EXPIRE_SECONDS 30 // 心跳续期时长（秒）：超过该时长未收到心跳，在线 key 过期，用户判定为离线

#define SERVER_LIST "serverlist"                    // hash：服务名 -> 对外地址(host:port)，服务发现用
#define SERVER_HEARTBEAT_PREFIX "serverheartbeat_"  // 存活 key 前缀：serverheartbeat_<name>，EX 30s 表示实例存活
#define SERVER_HEARTBEAT_RENEW_SECONDS 10           // 存活 key 的续期间隔（秒），必须显著小于 TTL，留出容错

#define USER_OFFLINE_MESSAGE_STREAM "offlinestream_" // 用户离线消息队列 + uid
#define GROUP_OFFLINE_MESSAGE_STREAM "offlinestream_group_" // 群聊用户离线消息队列 + uid
#define USER_XGROUP_NAME "userxgroup_" // 用户聊天消息消费组 + uid

#endif //GATESERVER_REDISMANAGER_H
