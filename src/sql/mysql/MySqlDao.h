/// mysql 读写层
#ifndef GATESERVER_MYSQLDAO_H
#define GATESERVER_MYSQLDAO_H

#include <queue>

#include "../../const.h"
#include <boost/mysql/tcp_ssl.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

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
    int registerUser(const std::string &email, const std::string &name, const std::string &password) const;

    bool checkIsAccountMatchEmail(const std::string &email, int account) const;
    bool updatePassword(int account, const std::string &password) const;
    bool chechIsPasswordMAtchAccount(const std::string &password, int account) const;

private:
    std::unique_ptr<SqlConnectionPool> m_pool;
};

#endif //GATESERVER_MYSQLDAO_H
