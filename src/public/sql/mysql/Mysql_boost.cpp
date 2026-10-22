#include "Mysql_boost.h"

#include "ConfigManager.h"
#include "MySqlDao.h"
#include "utils/PasswordVerify.h"

void Mysql_boost::start(net::io_context& io_context, mysql::pool_params params)
{
    m_pool = std::make_unique<mysql::connection_pool>(io_context, std::move(params));
    m_pool->async_run(net::detached);
}

Mysql_boost::~Mysql_boost() = default;

net::awaitable<MysqlResult<std::uint64_t> > Mysql_boost::registerUser(std::string email, std::string name, const std::string password)
{
    auto argon2id_password = PasswordVerify::hashPassword(password); // argon2id加密密码
    co_return co_await callMysqlSpWithResult("CALL reg_user(?, ?, ?, @result, @error)", name, email, argon2id_password);
}

net::awaitable<MysqlResult<std::uint64_t> > Mysql_boost::sendFriendRequest(std::uint64_t applicant_uid, std::uint64_t receiver_uid, std::string verify_message, std::string remark)
{
    co_return co_await callMysqlSpWithResult("CALL send_friend_request(?, ?, ?, ?, @result, @error)", applicant_uid, receiver_uid, verify_message, remark);
}

net::awaitable<MysqlResult<std::uint64_t> > Mysql_boost::handleFriendRequest(std::uint64_t request_id, std::uint64_t receiver_uid, FriendApplyStatus status)
{
    co_return co_await callMysqlSpWithResult("CALL handle_friend_request(?, ?, ?, @result, @error)", request_id, receiver_uid, status);
}

net::awaitable<int> Mysql_boost::deleteFriend(std::uint64_t uid, std::uint64_t friend_uid)
{
    co_return co_await callMysqlSpWithoutResult("CALL del_friend(?, ?, @error)", uid, friend_uid);
}

net::awaitable<int> Mysql_boost::updateFriendRemark(std::uint64_t uid, std::uint64_t friend_uid, std::string remark)
{
    co_return co_await callMysqlSpWithoutResult("CALL update_friend_remark(?, ?, ?, @error)", uid, friend_uid, remark);
}

net::awaitable<MysqlResult<std::uint64_t> > Mysql_boost::getNotifyUserIdInAuthFriendReq(std::uint64_t add_friend_request_id)
{
    co_return co_await callMysqlSpWithResult("CALL get_request_sender_uid(?, @result, @error)", add_friend_request_id);
}

// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<bool> > Mysql_boost::checkIsAccountMatchEmail(std::string email, std::uint64_t uid)
{
    MysqlResult<bool> root{};
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT 1 FROM user WHERE uid = ? AND email = ? LIMIT 1", net::use_awaitable);
            co_await connection->async_execute(sql.bind(uid, email), result, net::use_awaitable);
        }
        root.error_code = 0;
        if (result.rows().empty())
        {
            root.value = false;
        }
        else
        {
            root.value = true;
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }
    co_return root;
}


// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<bool> > Mysql_boost::updatePassword(std::uint64_t uid, const std::string new_password)
{
    MysqlResult<bool> root{};
    try
    {
        mysql::results result;
        {
            auto argon2id_password = PasswordVerify::hashPassword(new_password); // argon2id加密密码
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("UPDATE user SET password = ? WHERE uid = ?", net::use_awaitable);
            co_await connection->async_execute(sql.bind(argon2id_password, uid), result, net::use_awaitable);
        }
        if (result.affected_rows() == 0)
        {
            SPDLOG_WARN("[MySqlDao] update password affected 0 rows, uid: {}", uid);
            root.error_code = -1; // 没有密码被更新
            root.value = false;
        }
        else
        {
            root.error_code = 0;
            root.value = true;
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }
    co_return root;
}

// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<bool> > Mysql_boost::checkIsPasswordMatchAccount(std::uint64_t uid, const std::string password)
{
    MysqlResult<bool> root{};
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT password FROM user WHERE uid = ? LIMIT 1", net::use_awaitable); // 获取用户密码
            co_await connection->async_execute(sql.bind(uid), result, net::use_awaitable);
        }
        if (result.rows().empty())
        {
            root.error_code = -1; // 不存在的uid
            root.value = false;
        }
        else
        {
            root.error_code = 0;
            const auto argon2id_password = result.rows().at(0).at(0).as_string();
            root.value = PasswordVerify::verifyPassword(password, argon2id_password);
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }
    co_return root;
}

// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<std::optional<UserInfo> > > Mysql_boost::getUser(std::uint64_t uid)
{
    MysqlResult<std::optional<UserInfo> > root{};
    root.value = std::nullopt;
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT `name`, `email`, `desc`, `icon`, `sex` FROM `user` WHERE `uid` = ? LIMIT 1", net::use_awaitable); // 获取用户信息
            co_await connection->async_execute(sql.bind(uid), result, net::use_awaitable);
        }
        root.error_code = 0;
        if (!result.rows().empty())
        {
            const auto& row = result.rows().at(0);
            root.value = {
                {},
                row.at(0).as_string(),
                row.at(1).as_string(),
                row.at(2).is_null() ? std::string() : std::string(row.at(2).as_string()),
                row.at(3).is_null() ? std::string() : std::string(row.at(3).as_string()),
                uid,
                static_cast<Sex>(row.at(4).is_null() ? 3 : row.at(4).as_int64())
            };
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }

    co_return root;
}

// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<std::vector<UserInfo> > > Mysql_boost::getUser(std::string name)
{
    MysqlResult<std::vector<UserInfo> > root{};
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT `uid`, `email`, `desc`, `icon`, `sex` FROM `user` WHERE `name` = ? ", net::use_awaitable);
            co_await connection->async_execute(sql.bind(name), result, net::use_awaitable);
        }
        if (result.rows().empty())
        {
            root.error_code = -1; // 找不到用户
        }
        else
        {
            root.error_code = 0;
            root.value.reserve(result.rows().size());
            for (const auto& row: result.rows())
            {
                root.value.emplace_back(
                    std::string(),
                    name,
                    row.at(1).as_string(),
                    row.at(2).is_null() ? std::string() : std::string(row.at(2).as_string()),
                    row.at(3).is_null() ? std::string() : std::string(row.at(3).as_string()),
                    row.at(0).as_uint64(),
                    static_cast<Sex>(row.at(4).is_null() ? 3 : row.at(4).as_int64())
                    );
            }
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }

    co_return root;
}

net::awaitable<MysqlResult<std::vector<ApplyInfo> > > Mysql_boost::getFriendRequestList(std::uint64_t uid)
{
    MysqlResult<std::vector<ApplyInfo> > root{};
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT `id`, `applicant_uid`, `receiver_uid`, `status`, `message`, `created_at`, `updated_at` , `remark` FROM `friend_request` WHERE (`applicant_uid` = ? OR `receiver_uid` = ?) ORDER BY created_at DESC", net::use_awaitable);
            co_await connection->async_execute(sql.bind(uid, uid), result, net::use_awaitable);
        }
        root.error_code = 0;
        if (!result.rows().empty())
        {
            root.value.reserve(result.rows().size());
            for (const auto& row: result.rows())
            {
                const auto is_send = row.at(1).as_uint64() == uid; // 是否为发送方
                const auto [error_code, value] = co_await getUser(is_send ? row.at(2).as_uint64() : row.at(1).as_uint64());

                if (error_code != 0)
                {
                    continue;
                }

                const auto& info = value.value();
                root.value.emplace_back(
                    row.at(0).as_uint64(),
                    row.at(1).as_uint64(),
                    row.at(2).as_uint64(),
                    info.name,
                    row.at(4).is_null() ? std::string() : std::string(row.at(4).as_string()),
                    info.icon,
                    (!is_send || row.at(7).is_null()) ? std::string() : std::string(row.at(7).as_string()), // 如果不是发送方或备注为空，则返回空备注
                    info.sex,
                    static_cast<FriendApplyStatus>(row.at(3).as_int64())
                    );
            }
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }

    co_return root;
}

// ReSharper disable once CppMemberFunctionMayBeConst
net::awaitable<MysqlResult<std::vector<UidAndRemarkInUserFriend> > > Mysql_boost::getUserFriendList(std::uint64_t uid)
{
    MysqlResult<std::vector<UidAndRemarkInUserFriend> > root{};
    try
    {
        mysql::results result;
        {
            auto connection = co_await m_pool->async_get_connection(net::use_awaitable);
            const auto sql = co_await connection->async_prepare_statement("SELECT `friend_uid`, `remark` FROM `user_friend` WHERE `uid` = ?", net::use_awaitable);
            co_await connection->async_execute(sql.bind(uid), result, net::use_awaitable);
        }
        if (result.rows().empty())
        {
            root.error_code = -1; // 找不到用户
        }
        else
        {
            root.error_code = 0;
            root.value.reserve(result.rows().size());
            for (const auto& row: result.rows())
            {
                root.value.emplace_back(
                    row.at(1).is_null() ? std::string() : std::string(row.at(1).as_string()),
                    row.at(0).as_uint64()
                    );
            }
        }
        co_return root;
    }
    catch (mysql::error_with_diagnostics& e)
    {
        SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
        SPDLOG_ERROR("[MySqlDao] Server diagnostics: {}", e.get_diagnostics().server_message());
        root.error_code = -11;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        root.error_code = -11;
    }

    co_return root;
}

net::awaitable<int> Mysql_boost::changeUserInfo(uint64_t uid, std::optional<std::string> name, std::optional<std::string> desc, std::optional<std::string> icon, std::optional<Sex> sex)
{
    co_return co_await callMysqlSpWithoutResult("CALL update_user_info(?, ?, ?, ?, ?, @error)", uid, name, desc, icon, sex);
}

net::awaitable<MysqlResult<std::uint64_t> > Mysql_boost::createChatGroup(std::string group_name, std::uint64_t owner_id, std::string avatar_url, std::uint32_t max_members, std::string announcement)
{
    co_return co_await callMysqlSpWithResult("CALL update_user_info(?, ?, ?, ?, ?, @result, @error)", group_name, avatar_url, owner_id, max_members, announcement);
}

net::awaitable<int> Mysql_boost::addGroupMember(std::uint64_t group_id, std::uint64_t uid, std::uint64_t operator_id)
{
    co_return co_await callMysqlSpWithoutResult("CALL add_group_member(?, ?, ?, @error)", group_id, uid, operator_id);
}

net::awaitable<int> Mysql_boost::kickGroupMember(std::uint64_t group_id, std::uint64_t uid, std::uint64_t operator_id)
{
    co_return co_await callMysqlSpWithoutResult("CALL kick_group_member(?, ?, ?, @error)", group_id, uid, operator_id);
}

net::awaitable<int> Mysql_boost::dismissGroup(std::uint64_t group_id, std::uint64_t operator_id)
{
    co_return co_await callMysqlSpWithoutResult("CALL dismiss_group(?, ?, @error)", group_id, operator_id);
}
