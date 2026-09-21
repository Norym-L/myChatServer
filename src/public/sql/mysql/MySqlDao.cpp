#include "MySqlDao.h"
#include <boost/mysql.hpp>
#include <spdlog/spdlog.h>

#include <ConfigManager.h>
#include <utils/PasswordVerify.h>

SqlConnection::SqlConnection(mysql::tcp_ssl_connection connection, const std::int64_t last_operation_time)
    : connection(std::make_unique<mysql::tcp_ssl_connection>(std::move(connection))),
      last_operation_time(last_operation_time)
{}

SqlConnectionPool::SqlConnectionPool(
    const std::string_view host,
    int port,
    const std::string_view user,
    const std::string_view password,
    const std::string_view database,
    const int pool_size)
    : m_host(host), m_port(port), m_user(user), m_password(password), m_database(database), m_pool_size(pool_size)
{
    m_io_context = std::make_unique<asio::io_context>();

    // 创建 SSL context
    auto ssl_ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);

    // 解析主机名
    asio::ip::tcp::resolver resolver(m_io_context->get_executor());
    auto endpoints = resolver.resolve(m_host, std::to_string(m_port));

    // 连接参数
    mysql::handshake_params params(
        m_user.c_str(),
        m_password.c_str(),
        m_database.c_str()
        );

    for (int i = 0; i < pool_size; ++i)
    {
        try
        {
            // 创建连接对象
            auto connection = std::make_unique<mysql::tcp_ssl_connection>(m_io_context->get_executor(), *ssl_ctx);

            // 连接到 MySQL 服务器
            connection->connect(*endpoints.begin(), params);

            // 包装成 SqlConnection 并加入连接池
            auto sql_conn = std::make_unique<SqlConnection>(std::move(*connection), std::time(nullptr));
            m_pool.push(std::move(sql_conn));
        }
        catch (const std::exception& e)
        {
            spdlog::error("[MySqlDao] MySQL ConnectionPool: create connection failed [ NO.{} ] - {}", i, e.what());
        }
    }
}

SqlConnectionPool::~SqlConnectionPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_pool.empty())
    {
        m_pool.pop();
    }
    close();
}

std::unique_ptr<SqlConnection> SqlConnectionPool::getConnection()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this] { return !m_pool.empty() || m_be_closed; });
    if (m_be_closed)
    {
        return nullptr;
    }

    auto connection = std::move(m_pool.front());
    m_pool.pop();
    return std::move(connection);
}

void SqlConnectionPool::returnConnection(std::unique_ptr<SqlConnection> connection)
{
    if (m_be_closed)
    {
        return;
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_pool.push(std::move(connection));
    m_condition.notify_one();
}

void SqlConnectionPool::close()
{
    m_be_closed = true;
    m_condition.notify_all();
}

MySqlDao::MySqlDao()
{
    auto& config = ConfigManager::GetInstance();
    m_pool = std::make_unique<SqlConnectionPool>(
        config["Mysql"]["host"],
        std::stoi(config["Mysql"]["port"]),
        "root",
        config["Mysql"]["passwd"],
        "chatserver",
        3);
}

MySqlDao::~MySqlDao()
{
    m_pool->close();
}

int MySqlDao::registerUser(const std::string& email, const std::string& name, const std::string& password) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return -1;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("CALL reg_user(?, ?, ?, @result)");
        auto argon2id_password = PasswordVerify::hashPassword(password); // argon2id加密密码
        mysql::results result;
        connection->connection->execute(sql.bind(name, email, argon2id_password), result);

        mysql::results out_result;
        connection->connection->execute("SELECT @result", out_result);
        if (!out_result.rows().empty())
        {
            int ret_code = out_result.rows().at(0).at(0).as_int64();
            spdlog::info("[MySqlDao] register user return code: {}", ret_code);
            m_pool->returnConnection(std::move(connection));
            return ret_code;
        }

        m_pool->returnConnection(std::move(connection));
        return -1;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
    }

    m_pool->returnConnection(std::move(connection));
    return -1;
}

bool MySqlDao::checkIsAccountMatchEmail(const std::string& email, int account) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return false;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT 1 FROM user WHERE uid = ? AND email = ? LIMIT 1"); // 检查用户id
        mysql::results result;
        connection->connection->execute(sql.bind(account, email), result);

        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return false;
        }

        m_pool->returnConnection(std::move(connection));
        return true;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
    }

    m_pool->returnConnection(std::move(connection));
    return false;
}

bool MySqlDao::updatePassword(const int account, const std::string& password) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return false;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("UPDATE user SET password = ? WHERE uid = ?");
        auto argon2id_password = PasswordVerify::hashPassword(password); // argon2id加密密码
        mysql::results result;
        connection->connection->execute(sql.bind(argon2id_password, account), result);

        m_pool->returnConnection(std::move(connection));
        return true;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
    }

    m_pool->returnConnection(std::move(connection));
    return false;
}

bool MySqlDao::chechIsPasswordMAtchAccount(const std::string& password, int account) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return false;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT password FROM user WHERE uid = ? LIMIT 1"); // 获取用户密码
        mysql::results result;
        connection->connection->execute(sql.bind(account), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return false;
        }

        auto argon2id_password = result.rows().at(0).at(0).as_string();
        m_pool->returnConnection(std::move(connection));

        return PasswordVerify::verifyPassword(password, argon2id_password);
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
    }

    m_pool->returnConnection(std::move(connection));
    return false;
}

std::shared_ptr<UserInfo> MySqlDao::getUser(const size_t uid) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return nullptr;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT name FROM user WHERE uid = ? LIMIT 1"); // 获取用户名字
        mysql::results result;
        connection->connection->execute(sql.bind(uid), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return nullptr;
        }

        m_pool->returnConnection(std::move(connection));

        auto user_info = std::make_shared<UserInfo>(
            nullptr,
            result.rows().at(0).at(0).as_string(),
            uid);
        return user_info;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
    }

    m_pool->returnConnection(std::move(connection));
    return nullptr;
}
