#ifndef GATESERVER_MYSQL_BOOST_H
#define GATESERVER_MYSQL_BOOST_H

#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/pool_params.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/with_params.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include "data/UserInfo.h"

namespace net = boost::asio;
namespace mysql = boost::mysql;

template<typename T>
struct MysqlResult
{
    int error_code;
    T value;
};

class Mysql_boost
{
public:
    void start(net::io_context& io_context, mysql::pool_params params);
    ~Mysql_boost();
    /**
      * 注册用户
      * @param email 邮箱
      * @param name 用户名
      * @param password 密码
      * @return 成功返回用户uid  -1 一个邮箱创建不能多于5个账号 -10 代表数据库异常
      */
    net::awaitable<MysqlResult<std::uint64_t> > registerUser(std::string email, std::string name, std::string password);
    /**
     * 发送好友请求
     * @param applicant_uid 发送请求用户id
     * @param receiver_uid 接收请求用户id
     * @param verify_message 验证信息
     * @param remark 用户给对方的备注
     * @return 接收返回的好友请求编号， 0 申请成功， -1 申请人不存在 -2 接收人不存在 -3 不能添加自己 -4 已经是好友 -5 对方已申请你，请直接同意 -10 代表数据库异常
     */
    net::awaitable<MysqlResult<std::uint64_t> > sendFriendRequest(std::uint64_t applicant_uid, std::uint64_t receiver_uid, std::string verify_message, std::string remark);
    /**
     * 处理好友请求并接收发送者的uid
     * @param request_id 好友请求编号
     * @param receiver_uid 接收请求用户id
     * @param status 验证状态
     * @return 发送者的uid， 0 处理成功 -1 申请不存在或无权处理 -2 该申请已被处理过 -10 代表数据库异常
     */
    net::awaitable<MysqlResult<std::uint64_t> > handleFriendRequest(std::uint64_t request_id, std::uint64_t receiver_uid, FriendApplyStatus status);
    /**
     * 删除好友
     * @param uid 删除者的uid
     * @param friend_uid 被删除者的uid
     * @return 0 成功 -1 代表好友关系不存在 -10 代表数据库异常
     */
    net::awaitable<int> deleteFriend(std::uint64_t uid, std::uint64_t friend_uid);
    /**
     * 修改好友备注
     * @param uid 自己uid
     * @param friend_uid 好友uid
     * @param remark 备注
     * @return 0 代表设置成功 -1 对方不是你的好友，无法设置备注 -2 不能给自己设置备注 -10 代表数据库异常
     */
    net::awaitable<int> updateFriendRemark(std::uint64_t uid, std::uint64_t friend_uid, std::string remark);
    /**
     * 获取处理好友请求需要通知对方的uid
     * @param add_friend_request_id 好友亲求编号
     * @return 发送方的uid， 0 成功 -1 申请不存在 -10 代表数据库异常
     */
    net::awaitable<MysqlResult<std::uint64_t> > getNotifyUserIdInAuthFriendReq(std::uint64_t add_friend_request_id);
    net::awaitable<MysqlResult<bool> > checkIsAccountMatchEmail(std::string email, std::uint64_t uid);
    net::awaitable<MysqlResult<bool> > updatePassword(std::uint64_t uid, std::string new_password);
    net::awaitable<MysqlResult<bool> > checkIsPasswordMatchAccount(std::uint64_t uid, std::string password);
    net::awaitable<MysqlResult<std::optional<UserInfo> > > getUser(std::uint64_t uid);
    net::awaitable<MysqlResult<std::vector<UserInfo> > > getUser(std::string name);
    net::awaitable<MysqlResult<std::vector<ApplyInfo> > > getFriendRequestList(std::uint64_t uid);
    net::awaitable<MysqlResult<std::vector<UidAndRemarkInUserFriend> > > getUserFriendList(std::uint64_t uid);
    /**
     * 用户信息修改
     * @param uid uid
     * @param name 名称
     * @param desc 个性签名
     * @param icon 头像
     * @param sex 性别
     * @return 0 成功
     */
    net::awaitable<int> changeUserInfo(std::uint64_t uid, std::optional<std::string> name, std::optional<std::string> desc, std::optional<std::string> icon, std::optional<Sex> sex);
    // 群聊操作
    /**
     * 创建群聊
     * @param group_name 群名称
     * @param owner_id 群主id
     * @param avatar_url 群头像
     * @param max_members 最大群员
     * @param announcement 群公告
     * @return 群id, 0 代表新建成功, -1 代表用户不存在, -10 代表数据库异常
     */
    net::awaitable<MysqlResult<std::uint64_t> > createChatGroup(std::string group_name, std::uint64_t owner_id, std::string avatar_url = "", std::uint32_t max_members = 500, std::string announcement = "");

    /**
     * 邀请入群
     * @param group_id 群id
     * @param uid 新成员
     * @param operator_id 邀请者
     * @return 0 成功, -1 代表群不存在, -2 代表操作人权限不足, -3 代表用户已在群内, -4 代表群人数已满, -10 代表数据库异常
     */
    net::awaitable<int> addGroupMember(std::uint64_t group_id, std::uint64_t uid, std::uint64_t operator_id);

    /**
     * 踢出群聊
     * @param group_id 群id
     * @param uid 被踢者
     * @param operator_id 操作者
     * @return 0 成功, -1 代表群不存在, -2 代表不能踢出群主, -3 代表目标用户不在群内, -4 代表操作人权限不足, -5 代表管理员不能互踢, -10 代表数据库异常
     */
    net::awaitable<int> kickGroupMember(std::uint64_t group_id, std::uint64_t uid, std::uint64_t operator_id);

    /**
     * 解散群聊
     * @param group_id 群id
     * @param operator_id 操作者
     * @return  0 代表解散成功, -1 代表群不存在, -2 代表权限不足（只有群主能解散), -10 代表数据库异常
     */
    net::awaitable<int> dismissGroup(std::uint64_t group_id, std::uint64_t operator_id);

    /**
     * 获取群成员信息
     * @param group_id 群id
     * @return  0 查询成功, -1 代表群不存在, -11 代表数据库异常
     */
    net::awaitable<MysqlResult<std::vector<GroupUserInfo> > > getGroupMemberInfos(std::uint64_t group_id);

    /**
     * 获取群聊信息
     * @param group_id 群id
     * @return 0 查询成功, -1 代表群不存在, -11 代表数据库异常
     */
    net::awaitable<MysqlResult<GroupInfo> > getGroupInfo(std::uint64_t group_id) const;

    /**
     * 修改群聊信息（std::optional 为空表示该字段延用原值，绑定为 NULL）
     * @param group_id 群id
     * @param operator_id 操作者uid（需为群主或管理员）
     * @param name 群名称
     * @param avatar_url 群头像，传空串可清空
     * @param announcement 群公告，传空串可清空
     * @param max_members 最大成员数，不能小于当前成员数
     * @return 0 成功, -1 群不存在, -2 权限不足, -3 参数非法, -11 数据库异常
     */
    net::awaitable<int> changeGroupInfo(std::uint64_t group_id, std::uint64_t operator_id,
                                        std::optional<std::string> name = std::nullopt,
                                        std::optional<std::string> avatar_url = std::nullopt,
                                        std::optional<std::string> announcement = std::nullopt,
                                        std::optional<std::uint32_t> max_members = std::nullopt);

    /**
     * 聊天消息落库（历史存储）
     *
     * 一条 INSERT 完成，不走存储过程：消息是最高频写入，CALL + 再查 @error 要两次往返。
     * 唯一键 `(sender_id, client_msg_id)` 负责幂等，重复的客户端重试会被 `ON DUPLICATE KEY
     * UPDATE id=id` 静默吞掉（不新增行，也不报错）。
     *
     * @param sender_id 发送者uid
     * @param session_id 会话id：单聊=对方uid，群聊=群id（与投递语义一致，都是 to_uid）
     * @param is_group 是否群聊
     * @param msg_type 消息类型，对齐 chat_message::Type（1文本/2图片/3文件/4表情包）
     * @param content 消息体
     * @param client_msg_id 客户端消息id（msg_id），用于重试幂等；没有则传 std::nullopt
     * @return 0 已入库（重复消息同样返回 0），-10 代表数据库异常
     */
    net::awaitable<int> saveMessage(std::uint64_t sender_id, std::uint64_t session_id, bool is_group, int msg_type,
                                    std::string content, std::optional<std::string> client_msg_id);

private:
    std::unique_ptr<mysql::connection_pool> m_pool;

    template<typename... Args>
    net::awaitable<int> callMysqlSpWithoutResult(std::string sql, Args&&... args)
    {
        auto local_args = std::make_tuple(std::forward<Args>(args)...);
        try
        {
            mysql::pooled_connection connection = co_await m_pool->async_get_connection(net::use_awaitable);
            auto statement = co_await connection->async_prepare_statement(sql, net::use_awaitable);
            auto bound_stmt = std::apply([&statement]<typename... type>(type&&... unpacked_args)
                                             {
                                                 return statement.bind(std::forward<type>(unpacked_args)...);
                                             },
                                         std::move(local_args));
            mysql::results result;
            co_await connection->async_execute(bound_stmt, result, net::use_awaitable);
            mysql::results out_result;
            co_await connection->async_execute("SELECT @error", out_result, net::use_awaitable);
            if (!out_result.rows().empty() && !out_result.rows().at(0).empty())
            {
                const auto& field = out_result.rows().at(0).at(0);
                if (field.is_null())
                {
                    SPDLOG_WARN("[MySqlDao] SP did not set @error, defaulting to -1");
                    co_return -1;
                }
                int ret_code = static_cast<int>(field.as_int64());
                SPDLOG_INFO("[MySqlDao] SP return code: {}", ret_code);
                co_return ret_code;
            }
            co_return -11;
        }
        catch (const mysql::error_with_diagnostics& e)
        {
            SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
            SPDLOG_ERROR("[MySqlDao] Server message: {}", e.get_diagnostics().server_message());
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        }
        co_return -11;
    }

    template<typename... Args>
    net::awaitable<MysqlResult<std::uint64_t> > callMysqlSpWithResult(std::string sql, Args&&... args)
    {
        auto local_args = std::make_tuple(std::forward<Args>(args)...);
        MysqlResult<std::uint64_t> root{};
        try
        {
            mysql::pooled_connection connection = co_await m_pool->async_get_connection(net::use_awaitable);
            auto statement = co_await connection->async_prepare_statement(sql, net::use_awaitable);
            auto bound_stmt = std::apply([&statement]<typename... type>(type&&... unpacked_args)
                                             {
                                                 return statement.bind(std::forward<type>(unpacked_args)...);
                                             },
                                         std::move(local_args));
            mysql::results exec_result;
            co_await connection->async_execute(bound_stmt, exec_result, net::use_awaitable);
            mysql::results out_result;
            co_await connection->async_execute("SELECT @result, @error", out_result, net::use_awaitable);
            if (!out_result.rows().empty() && out_result.rows().at(0).size() >= 2)
            {
                const auto& row = out_result.rows().at(0);
                root.value = row.at(0).is_uint64() ? row.at(0).as_uint64() : 0;
                root.error_code = row.at(1).is_int64() ? static_cast<int>(row.at(1).as_int64()) : -1;
                SPDLOG_INFO("[MySqlDao] SP return code: {}", root.error_code);
                co_return root;
            }
            root.error_code = -11;
            co_return root;
        }
        catch (const mysql::error_with_diagnostics& e)
        {
            SPDLOG_ERROR("[MySqlDao] MySQL error: {}", e.what());
            SPDLOG_ERROR("[MySqlDao] Server message: {}", e.get_diagnostics().server_message());
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("[MySqlDao] Error: {}", e.what());
        }

        root.error_code = -11;
        co_return root;
    }
};


#endif //GATESERVER_MYSQL_BOOST_H
