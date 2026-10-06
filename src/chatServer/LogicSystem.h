#ifndef GATESERVER_LOGICSYSTEM_H
#define GATESERVER_LOGICSYSTEM_H

#include <queue>
#include <thread>
#include <map>
#include <functional>

#include <Singleton.h>
#include <const.h>

class LogicNode;
class CSession;

using Function_CallBack = std::function<void(std::shared_ptr<CSession>, const short& message_id, const std::string& data)>;

class LogicSystem : public TSingLeton<LogicSystem>
{
    friend class TSingLeton;

public:
    ~LogicSystem();

    void postMessageToQueue(const std::shared_ptr<LogicNode>& logic_node);

private:
    LogicSystem();
    void dealMessage();

    void registerCallBacks();
    void loginHandler(const std::shared_ptr<CSession>&, short message_id, const std::string& message_data);

    std::mutex m_mutex;
    std::queue<std::shared_ptr<LogicNode> > m_messageQueue;
    std::condition_variable m_conditionVariable;
    std::thread m_workThread;
    bool m_beStop = false;
    std::map<short, Function_CallBack> m_callbackFunctions;
    std::unordered_map<size_t, std::shared_ptr<UserInfo>> m_users;
};


#endif //GATESERVER_LOGICSYSTEM_H
