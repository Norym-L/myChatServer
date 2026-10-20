#ifndef GATESERVER_LOGICSYSTEM_H
#define GATESERVER_LOGICSYSTEM_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_context.hpp>

#include <Singleton.h>
#include <const.h>

#include "data/UserInfo.h"

namespace net = boost::asio;

class LogicNode;
class CSession;

// 消息处理回调：每个请求都由一个协程处理，内部用 co_await 访问 Redis
using Function_CallBack = std::function<net::awaitable<void>(std::shared_ptr<CSession>, const std::string& data)>;

class LogicSystem : public TSingLeton<LogicSystem>
{
    friend class TSingLeton;

public:
    ~LogicSystem();

    void postMessageToQueue(const std::shared_ptr<LogicNode>& logic_node);
    /// 逻辑线程的 io_context：同时承载 Redis 连接，业务协程与 Redis 命令都跑在这一条线程上
    net::io_context& getIoContext() { return m_ioContext; }
    /// 用户断开连接：异步删除在线状态 key（不等 30s TTL 自然过期）
    void offlineUser(std::uint64_t uid);
    // 获取用户信息
    static net::awaitable<std::shared_ptr<UserInfo> > GetBaseInfo(const std::string& base_key, std::uint64_t uid);

private:
    LogicSystem();
    net::awaitable<void> dealMessage();
    static net::awaitable<void> RemoveOnlineKey(std::uint64_t uid);

    void registerCallBacks();
    net::awaitable<void> loginHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 心跳续期：刷新在线状态 key 的过期时间
    net::awaitable<void> heartbeatHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> syncFriendRequestListHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> checkIsUserOnlineHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> authFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> sendTextMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 获取好友列表
    net::awaitable<void> syncUserFriendList(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 同步聊天消息 客户端开始统计消息条数 > 10 或是 超时 则拉取消息
    net::awaitable<void> syncChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 客户端确认收到聊天消息 客户端收到消息->发送服务器确认
    net::awaitable<void> ackChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    net::awaitable<void> delFriendHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;


    [[nodiscard]] static bool GrpcSendFriendRequestToOther(std::uint64_t add_friend_request_id, std::uint64_t to_uid, const std::string& user_ip);
    [[nodiscard]] static bool GrpcAuthFriendRequestAndNotifyToOther(std::uint64_t sender_uid, std::uint64_t add_friend_request_id, int state, const std::string& user_ip); // FriendApplyStatus
    [[nodiscard]] static bool GrpcNotifyReceiveMessageToOther(std::uint64_t to_uid, const std::string& user_ip);
    [[nodiscard]] static net::awaitable<std::shared_ptr<UserInfo> > SearchUserByUid(uint64_t uid);
    [[nodiscard]] static net::awaitable<std::vector<std::shared_ptr<UserInfo> > > SearchUserByName(const std::string& name);

    static std::string Exception_ptr_to_string(const std::exception_ptr& ep)
    {
        if (!ep) return "no exception";
        try
        {
            std::rethrow_exception(ep);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unknown exception type";
        }
    }

    net::io_context m_ioContext;
    net::executor_work_guard<net::io_context::executor_type> m_workGuard;
    // 消息队列由 channel 承载：连接线程只做 try_send，逻辑线程串行地 co_await 消费
    net::experimental::concurrent_channel<void(boost::system::error_code, std::shared_ptr<LogicNode>)> m_messageChannel;
    std::thread m_workThread;
    std::string m_selfServerName;
    std::map<short, Function_CallBack> m_callbackFunctions;
};

#define HANDLER_PREFIX(message_type) \
Json::Reader reader;\
Json::Value root;\
reader.parse(message_data, root);\
Json::Value return_value;\
Defer defer([this, &return_value, session]()\
    {\
        (void) this;\
        const std::string return_str = return_value.toStyledString();\
        session->send(return_str.c_str(), return_str.size(), MessageType::message_type);\
    });\
if (session->getUserId() == 0)\
{\
    return_value["error"] = ErrorCode::USER_UID_INVALID;\
    co_return;\
}

#define HANDLER_PREFIX_WITHOUT_CHECK_LOGIN(message_type) \
Json::Reader reader;\
Json::Value root;\
reader.parse(message_data, root);\
Json::Value return_value;\
Defer defer([this, &return_value, session]()\
    {\
        (void) this;\
        const std::string return_str = return_value.toStyledString();\
        session->send(return_str.c_str(), return_str.size(), MessageType::message_type);\
    });

#define HANDLER_PREFIX_WITHOUT_RSP \
Json::Reader reader;\
Json::Value root;\
reader.parse(message_data, root);

#endif //GATESERVER_LOGICSYSTEM_H
