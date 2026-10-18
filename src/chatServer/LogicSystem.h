#ifndef GATESERVER_LOGICSYSTEM_H
#define GATESERVER_LOGICSYSTEM_H

#include <queue>
#include <thread>
#include <map>
#include <functional>

#include <Singleton.h>
#include <const.h>

#include "data/UserInfo.h"

class LogicNode;
class CSession;

using Function_CallBack = std::function<void(std::shared_ptr<CSession>, const std::string& data)>;

class LogicSystem : public TSingLeton<LogicSystem>
{
    friend class TSingLeton;

public:
    ~LogicSystem();

    void postMessageToQueue(const std::shared_ptr<LogicNode>& logic_node);
    // 获取用户信息
    static std::shared_ptr<UserInfo> GetBaseInfo(const std::string& base_key, std::uint64_t uid);

private:
    LogicSystem();
    void dealMessage();

    void registerCallBacks();
    void loginHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 心跳续期：刷新在线状态 key 的过期时间
    void heartbeatHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void syncFriendRequestListHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void checkIsUserOnlineHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void authFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void sendTextMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 获取好友列表
    void syncUserFriendList(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 同步聊天消息 客户端开始统计消息条数 > 10 或是 超时 则拉取消息
    void syncChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 客户端确认收到聊天消息 客户端收到消息->发送服务器确认
    void ackChatMessageHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;

    [[nodiscard]] static bool GrpcSendFriendRequestToOther(std::uint64_t add_friend_request_id, std::uint64_t to_uid, const std::string& user_ip);
    [[nodiscard]] static bool GrpcAuthFriendRequestAndNotifyToOther(std::uint64_t sender_uid, std::uint64_t add_friend_request_id, int state, const std::string& user_ip); // FriendApplyStatus
    [[nodiscard]] static bool GrpcNotifyReceiveMessageToOther(std::uint64_t to_uid, const std::string& user_ip);
    [[nodiscard]] static std::shared_ptr<UserInfo> SearchUserByUid(uint64_t uid);
    [[nodiscard]] static std::vector<std::shared_ptr<UserInfo> > SearchUserByName(const std::string& name);

    std::mutex m_mutex;
    std::string m_selfServerName;
    std::queue<std::shared_ptr<LogicNode> > m_messageQueue;
    std::condition_variable m_conditionVariable;
    std::thread m_workThread;
    bool m_beStop = false;
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
    });

#define HANDLER_PREFIX_WITHOUT_RSP \
Json::Reader reader;\
Json::Value root;\
reader.parse(message_data, root);

#endif //GATESERVER_LOGICSYSTEM_H
