/// mysql 读写层
#ifndef GATESERVER_MYSQLDAO_H
#define GATESERVER_MYSQLDAO_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

#include <boost/mysql/tcp_ssl.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

#include "data/UserInfo.h"

/// 账号/密码校验结果：区分"业务不匹配"与"依赖不可用"，避免两者共用同一错误码
enum class MatchResult
{
    Matched,     // 匹配
    NotMatched,  // 不匹配
    Unavailable, // 依赖服务不可用（连接池获取超时、数据库异常等）
};

namespace mysql = boost::mysql;
namespace asio = boost::asio;

class SqlConnection
{
public:
    SqlConnection(mysql::tcp_ssl_connection connection, std::int64_t last_operation_time);
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
    SqlConnectionPool(std::string_view host, int port, std::string_view user, std::string_view password, std::string_view database, int pool_size);

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
     * @return 成功返回用户uid  -1 一个邮箱创建不能多于5个账号 -10 代表数据库异常
     */
    [[nodiscard]] int registerUser(const std::string& email, const std::string& name, const std::string& password) const;
    /**
     * 发送好友请求
     * @param add_friend_request_id 接收返回的好友请求编号
     * @param applicant_uid 发送请求用户id
     * @param receiver_uid 接收请求用户id
     * @param verify_message 验证信息
     * @param remark 用户给对方的备注
     * @return 0 申请成功， -1 申请人不存在 -2 接收人不存在 -3 不能添加自己 -4 已经是好友 -5 对方已申请你，请直接同意 -10 代表数据库异常
     */
    [[nodiscard]] int sendFriendRequest(std::uint64_t& add_friend_request_id, std::uint64_t applicant_uid, std::uint64_t receiver_uid, const std::string& verify_message, const std::string& remark) const;

    /**
     * 处理好友请求
     * @param request_id 好友请求编号
     * @param receiver_uid 接收请求用户id
     * @param status 验证状态
     * @return 0 处理成功 -1 申请不存在或无权处理 -2 该申请已被处理过 -10 代表数据库异常
     */
    [[nodiscard]] int handleFriendRequest(std::uint64_t request_id, std::uint64_t receiver_uid, FriendApplyStatus status) const;

    /**
     * 删除好友
     * @param uid 删除者的uid
     * @param friend_uid 被删除者的uid
     * @return 0 成功 -1 代表好友关系不存在 -10 代表数据库异常
     */
    [[nodiscard]] int deleteFriend(std::uint64_t uid, std::uint64_t friend_uid) const;
    /**
     * 修改好友备注
     * @param uid 自己uid
     * @param friend_uid 好友uid
     * @param remark 备注
     * @return 0 代表设置成功 -1 对方不是你的好友，无法设置备注 -2 不能给自己设置备注 -10 代表数据库异常
     */
    [[nodiscard]] int updateFriendRemark(std::uint64_t uid, std::uint64_t friend_uid, const std::string& remark) const;

    /// 校验 uid 与 email 是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsAccountMatchEmail(const std::string& email, std::uint64_t account) const;
    [[nodiscard]] bool updatePassword(std::uint64_t account, const std::string& password) const;
    /// 校验密码与账号是否匹配；依赖不可用时返回 MatchResult::Unavailable
    [[nodiscard]] MatchResult checkIsPasswordMatchAccount(const std::string& password, std::uint64_t account) const;
    [[nodiscard]] std::shared_ptr<UserInfo> getUser(std::uint64_t uid) const;
    [[nodiscard]] std::vector<std::shared_ptr<UserInfo> > getUser(const std::string& name) const;
    [[nodiscard]] std::vector<std::shared_ptr<ApplyInfo> > getFriendRequestListByUid(uint64_t uid);

private:
    template<typename... Args>
    int callMysqlSpWithResult(const std::string& sql, Args&&... args) const
    {
        auto connection = m_pool->getConnection();
        if (!connection) return -11;
        try
        {
            // 1. 预编译语句
            const auto statement = connection->connection->prepare_statement(sql);

            // 2. 参数打包成 tuple，然后绑定执行
            mysql::results result;
            connection->connection->execute(statement.bind(std::forward<Args>(args)...), result);

            // 3. 读取 @result 输出参数
            mysql::results out_result;
            connection->connection->execute("SELECT @result", out_result);

            m_pool->returnConnection(std::move(connection));

            if (!out_result.rows().empty() &&
                !out_result.rows().at(0).empty())
            {
                int ret_code = static_cast<int>(out_result.rows().at(0).at(0).as_int64());
                spdlog::info("[MySqlDao] SP return code: {}", ret_code);
                return ret_code;
            }
            return -11;
        }
        catch (const mysql::error_with_diagnostics& e)
        {
            spdlog::error("[MySqlDao] MySQL error: {}", e.what());
            spdlog::error("[MySqlDao] Server message: {}", e.get_diagnostics().server_message());
        }
        catch (const std::exception& e)
        {
            spdlog::error("[MySqlDao] Error: {}", e.what());
        }

        if (connection)
        {
            m_pool->returnConnection(std::move(connection));
        }
        return -11;
    }

    template<typename... Args>
    int callMysqlSpWithResultInUint(std::uint64_t& request_id, const std::string& sql, Args&&... args) const
    {
        auto connection = m_pool->getConnection();
        if (!connection) return -11;

        try
        {
            // 1. 预编译语句
            const auto statement = connection->connection->prepare_statement(sql);

            // 2. 参数打包成 tuple，然后绑定执行
            mysql::results result;
            connection->connection->execute(statement.bind(std::forward<Args>(args)...), result);

            // 3. 读取 @request_id, @error_code 输出参数
            mysql::results out_result;
            connection->connection->execute("SELECT @request_id, @error_code", out_result);

            m_pool->returnConnection(std::move(connection));

            if (!out_result.rows().empty() && !out_result.rows().at(0).empty())
            {
                const auto& row = out_result.rows().at(0);
                if (row.size() >= 2)
                {
                    // 第一列：request_id (BIGINT UNSIGNED -> uint64)
                    request_id = row.at(0).as_uint64();
                    const auto error_code = static_cast<int>(row.at(1).as_int64());
                    spdlog::debug("[MySqlDao] request_id: {}, error_code: {}", request_id, error_code);
                    // 第二列：error_code (INT -> int)
                    return error_code;
                }
            }
            return -11;
        }
        catch (const mysql::error_with_diagnostics& e)
        {
            spdlog::error("[MySqlDao] MySQL error: {}", e.what());
            spdlog::error("[MySqlDao] Server message: {}", e.get_diagnostics().server_message());
        }
        catch (const std::exception& e)
        {
            spdlog::error("[MySqlDao] Error: {}", e.what());
        }

        if (connection)
        {
            m_pool->returnConnection(std::move(connection));
        }
        return -11;
    }

    std::unique_ptr<SqlConnectionPool> m_pool;
};

#endif //GATESERVER_MYSQLDAO_H
