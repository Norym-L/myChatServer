/// mysql 读写层
#ifndef GATESERVER_MYSQLDAO_H
#define GATESERVER_MYSQLDAO_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

#include <const.h>
#include <boost/mysql/tcp_ssl.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include "data/UserInfo.h"

namespace mysql = boost::mysql;
namespace asio = boost::asio;

class SqlConnection
{
public:
    SqlConnection(mysql::tcp_ssl_connection connection, const std::int64_t last_operation_time);
    std::unique_ptr<mysql::tcp_ssl_connection> connection; // 连接
    std::int64_t last_operation_time;                      // 最后一次操作时间
};

/// 连接池
class SqlConnectionPool
{
public:
    /**
     * 创建连接池
     * @param host 地址
     * @param port 端口
     * @param user 用户
     * @param password 密码
     * @param database 数据库
     * @param pool_size 池大小
     * TODO sql断线重连
     */
    SqlConnectionPool(const std::string_view host, int port, const std::string_view user, const std::string_view password, const std::string_view database, const int pool_size);

    ~SqlConnectionPool();

    /// 获取连接
    std::unique_ptr<SqlConnection> getConnection();

    /// 归还连接
    void returnConnection(std::unique_ptr<SqlConnection> connection);

    /// 关闭连接池
    void close();

private:
    std::unique_ptr<asio::io_context> m_io_context = nullptr;
    std::unique_ptr<asio::ssl::context> m_ssl_ctx = nullptr; // 需长于池内连接的生命周期：连接按引用持有 SSL 上下文
    std::string m_host;
    int m_port;
    std::string m_user;
    std::string m_password;
    std::string m_database;
    int m_pool_size;
    std::queue<std::unique_ptr<SqlConnection> > m_pool;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_be_closed = false;
};

class MySqlDao
{
public:
    MySqlDao();
    ~MySqlDao();

    /**
     * 注册用户
     * @param email 邮箱
     * @param name 用户名
     * @param password 密码
     * @return 0 成功
     */
    [[nodiscard]] int registerUser(const std::string& email, const std::string& name, const std::string& password) const;

    /// 校验 uid 与 email 是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsAccountMatchEmail(const std::string& email, std::uint64_t account) const;
    [[nodiscard]] bool updatePassword(std::uint64_t account, const std::string& password) const;
    /// 校验密码与账号是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsPasswordMatchAccount(const std::string& password, std::uint64_t account) const;
    [[nodiscard]] std::shared_ptr<UserInfo> getUser(std::uint64_t uid) const;
    [[nodiscard]] std::vector<std::shared_ptr<UserInfo> > getUser(const std::string& name) const;

private:
    std::unique_ptr<SqlConnectionPool> m_pool;
};

#endif //GATESERVER_MYSQLDAO_H
