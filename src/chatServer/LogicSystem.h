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
    void searchUserHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void sendFriendRequestHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    void syncFriendRequestListHandler(const std::shared_ptr<CSession>& session, const std::string& message_data) const;
    // 获取好友列表，好友申请列表
    void getUserApplyListAndFriendList(const std::shared_ptr<CSession>& session, const std::string& message_data);

    [[nodiscard]] static bool GrpcSendFriendRequestToOther(std::uint64_t add_friend_request_id, std::uint64_t to_uid);
    [[nodiscard]] static std::shared_ptr<UserInfo> SearchUserByUid(uint64_t uid);
    [[nodiscard]] static std::vector<std::shared_ptr<UserInfo> > SearchUserByName(const std::string& name);

    std::mutex m_mutex;
    std::queue<std::shared_ptr<LogicNode> > m_messageQueue;
    std::condition_variable m_conditionVariable;
    std::thread m_workThread;
    bool m_beStop = false;
    std::map<short, Function_CallBack> m_callbackFunctions;
};


#endif //GATESERVER_LOGICSYSTEM_H
