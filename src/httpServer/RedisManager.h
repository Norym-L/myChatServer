#ifndef GATESERVER_REDISMANAGER_H
#define GATESERVER_REDISMANAGER_H

#include <hiredis/hiredis.h>

#include "../const.h"

class RedisManager : public TSingLeton<RedisManager>
{
    friend class TSingLeton;

public:
    ~RedisManager();
    bool connect(const std::string& host, int port);
    bool get(const std::string& key, std::string& value);
    bool set(const std::string& key, const std::string& value);
    bool auth(const std::string& password);
    bool lPush(const std::string& key, const std::string& value);
    bool lPop(const std::string& key, std::string& value);
    bool rPush(const std::string& key, const std::string& value);
    bool rPop(const std::string& key, std::string& value);
    bool hSet(const std::string& key, const std::string& h_key, const std::string& value);
    bool hSet(const char* key, const char* h_key, const char* h_value, std::size_t h_value_len);
    std::string hGet(const std::string& key, const std::string& h_key);
    bool del(const std::string& key);
    bool existsKey(const std::string& key);
    void close() const;

private:
    RedisManager();

    redisContext* m_connect;
    redisReply* m_reply;
};


#endif //GATESERVER_REDISMANAGER_H
