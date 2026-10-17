#include "MySqlDao.h"
#include <boost/mysql.hpp>
#include <chrono>
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

    // 创建 SSL context；连接对象按引用持有它，因此必须由连接池成员持有以保证生命周期长于池内连接
    m_ssl_ctx = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);

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
            auto connection = std::make_unique<mysql::tcp_ssl_connection>(m_io_context->get_executor(), *m_ssl_ctx);

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
    // 限时等待：池为空（如初始连接全部失败）时不允许永久阻塞调用线程，超时返回空由调用方判空
    if (!m_condition.wait_for(lock, std::chrono::seconds(2), [this] { return !m_pool.empty() || m_be_closed; }))
    {
        return nullptr;
    }
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
    auto argon2id_password = PasswordVerify::hashPassword(password); // argon2id加密密码
    return callMysqlSpWithResult("CALL reg_user(?, ?, ?, @result)", name, email, argon2id_password);
}

int MySqlDao::sendFriendRequest(std::uint64_t& add_friend_request_id, const std::uint64_t applicant_uid, const std::uint64_t receiver_uid, const std::string& verify_message, const std::string& remark) const
{
    return callMysqlSpWithResultInUint(add_friend_request_id, "CALL send_friend_request(?, ?, ?, ?, @request_id, @error_code)", applicant_uid, receiver_uid, verify_message, remark);
}

int MySqlDao::handleFriendRequest(std::uint64_t& sender_uid, const std::uint64_t request_id, const std::uint64_t receiver_uid, const FriendApplyStatus status) const
{
    return callMysqlSpWithResultInUint(sender_uid, "CALL handle_friend_request(?, ?, ?, @request_id, @error_code)", request_id, receiver_uid, status);
}

int MySqlDao::deleteFriend(const std::uint64_t uid, const std::uint64_t friend_uid) const
{
    return callMysqlSpWithResult("CALL del_friend(?, ?, @result)", uid, friend_uid);
}

int MySqlDao::updateFriendRemark(const std::uint64_t uid, const std::uint64_t friend_uid, const std::string& remark) const
{
    return callMysqlSpWithResult("CALL update_friend_remark(?, ?, @result)", uid, friend_uid, remark);
}

int MySqlDao::getNotifyUserIdInAuthFriendReq(std::uint64_t& sender_uid, const std::uint64_t add_friend_request_id) const
{
    return callMysqlSpWithResultInUint(sender_uid, "CALL get_request_sender_uid(?, @request_id, @error_code)", add_friend_request_id);
}

MatchResult MySqlDao::checkIsAccountMatchEmail(const std::string& email, const std::uint64_t account) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        // 连接池获取超时（数据库不可达），与"不匹配"区分开
        return MatchResult::Unavailable;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT 1 FROM user WHERE uid = ? AND email = ? LIMIT 1"); // 检查用户id
        mysql::results result;
        connection->connection->execute(sql.bind(account, email), result);

        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return MatchResult::NotMatched;
        }

        m_pool->returnConnection(std::move(connection));
        return MatchResult::Matched;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        m_pool->returnConnection(std::move(connection));
        return MatchResult::Unavailable;
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
        m_pool->returnConnection(std::move(connection));
        return MatchResult::Unavailable;
    }
}

bool MySqlDao::updatePassword(const std::uint64_t account, const std::string& password) const
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

        // uid 不存在时 UPDATE 命中 0 行，需按受影响行数判定失败
        if (result.affected_rows() == 0)
        {
            spdlog::warn("[MySqlDao] update password affected 0 rows, uid: {}", account);
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

MatchResult MySqlDao::checkIsPasswordMatchAccount(const std::string& password, const std::uint64_t account) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        // 连接池获取超时（数据库不可达），与"密码不匹配"区分开
        return MatchResult::Unavailable;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT password FROM user WHERE uid = ? LIMIT 1"); // 获取用户密码
        mysql::results result;
        connection->connection->execute(sql.bind(account), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return MatchResult::NotMatched;
        }

        auto argon2id_password = result.rows().at(0).at(0).as_string();
        m_pool->returnConnection(std::move(connection));

        return PasswordVerify::verifyPassword(password, argon2id_password)
                   ? MatchResult::Matched
                   : MatchResult::NotMatched;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        spdlog::error("[MySqlDao] MySQL error: {}", e.what());
        spdlog::error("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        m_pool->returnConnection(std::move(connection));
        return MatchResult::Unavailable;
    }
    catch (const std::exception& e)
    {
        spdlog::error("[MySqlDao] Error: {}", e.what());
        m_pool->returnConnection(std::move(connection));
        return MatchResult::Unavailable;
    }
}

std::shared_ptr<UserInfo> MySqlDao::getUser(const std::uint64_t uid) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return nullptr;
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT `name`, `email`, `desc`, `icon`, `sex` FROM `user` WHERE `uid` = ? LIMIT 1"); // 获取用户信息
        mysql::results result;
        connection->connection->execute(sql.bind(uid), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return nullptr;
        }

        m_pool->returnConnection(std::move(connection));

        const auto row = result.rows().at(0);
        auto user_info = std::make_shared<UserInfo>(
            std::string(), // token 置空字符串：用 nullptr 构造 std::string 属于未定义行为
            row.at(0).as_string(),
            row.at(1).as_string(),
            row.at(2).is_null() ? std::string() : std::string(row.at(2).as_string()), // desc
            row.at(3).is_null() ? std::string() : std::string(row.at(3).as_string()), // icon
            uid,
            static_cast<Sex>(row.at(4).is_null() ? 3 : row.at(4).as_int64()) // sex
            );
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

std::vector<std::shared_ptr<UserInfo> > MySqlDao::getUser(const std::string& name) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return {};
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT `uid`, `email`, `desc`, `icon`, `sex` FROM `user` WHERE `name` = ? "); // 获取用户信息
        mysql::results result;
        connection->connection->execute(sql.bind(name), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return {};
        }

        m_pool->returnConnection(std::move(connection));
        std::vector<std::shared_ptr<UserInfo> > user_infos;
        // const auto row = result.rows().at(0);
        for (const auto& row: result.rows())
        {
            auto user_info = std::make_shared<UserInfo>(
                std::string(), // token 置空字符串：用 nullptr 构造 std::string 属于未定义行为
                name,
                row.at(1).as_string(),
                row.at(2).is_null() ? std::string() : std::string(row.at(2).as_string()),
                row.at(3).is_null() ? std::string() : std::string(row.at(3).as_string()),
                row.at(0).as_uint64(),
                static_cast<Sex>(row.at(4).is_null() ? 3 : row.at(4).as_int64())
                );
            user_infos.push_back(user_info);
        }

        return user_infos;
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
    return {};
}

std::vector<std::shared_ptr<ApplyInfo> > MySqlDao::getFriendRequestListByUid(const uint64_t uid) const
{
    auto connection = m_pool->getConnection();
    if (!connection)
    {
        return {};
    }

    try
    {
        const auto sql = connection->connection->prepare_statement("SELECT `id`, `applicant_uid`, `receiver_uid`, `status`, `message`, `created_at`, `updated_at` , `remark` FROM `friend_request` WHERE (`applicant_uid` = ? OR `receiver_uid` = ?) ORDER BY created_at DESC"); // 获取与当前用户相关的好友请求（我发出的 + 我收到的）
        mysql::results result;
        connection->connection->execute(sql.bind(uid, uid), result);
        if (result.rows().empty())
        {
            m_pool->returnConnection(std::move(connection));
            return {};
        }

        m_pool->returnConnection(std::move(connection));
        std::vector<std::shared_ptr<ApplyInfo> > apply_infos;

        for (const auto& row: result.rows())
        {
            const auto is_send = row.at(1).as_uint64() == uid; // 是否为发送方
            const auto other_info = getUser(is_send ? row.at(2).as_uint64() : row.at(1).as_uint64());
            apply_infos.emplace_back(
                std::make_shared<ApplyInfo>(
                    row.at(0).as_uint64(),
                    row.at(1).as_uint64(),
                    row.at(2).as_uint64(),
                    other_info->name,
                    row.at(4).is_null() ? std::string() : std::string(row.at(4).as_string()),
                    other_info->icon,
                    (!is_send || row.at(7).is_null()) ? std::string() : std::string(row.at(7).as_string()), // 如果不是发送方或备注为空，则返回空备注
                    other_info->sex,
                    static_cast<FriendApplyStatus>(row.at(3).as_int64())
                    )
                );
        }

        return apply_infos;
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
    return {};
}
