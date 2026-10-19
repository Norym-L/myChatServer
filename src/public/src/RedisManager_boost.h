#ifndef GATESERVER_REDISMANAGER_BOOST_H
#define GATESERVER_REDISMANAGER_BOOST_H

class RedisManagerBoost
{
    // TODO boost协程redis接口实现
};


#define CODE_PREFIX "code_"
#define USER_IP_PERFIX "uip_"
#define USER_TOKEN_PERFIX "utoken_"
#define IP_COUNT_PERFIX "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define USER_BASE_INFO_BY_NAME "ubaseinfo_name_" // 需要处理多个值，连接用户uid数组
#define LOGIN_COUNT_PREFIX "logincount_" // 每实例连接数 key 前缀：logincount_<name>，EX 30s，随实例心跳续期

#define HEART_BEAT_EXPIRE_SECONDS 30 // 心跳续期时长（秒）：超过该时长未收到心跳，在线 key 过期，用户判定为离线

#define SERVER_LIST "serverlist"                    // hash：服务名 -> 对外地址(host:port)，服务发现用
#define SERVER_HEARTBEAT_PREFIX "serverheartbeat_"  // 存活 key 前缀：serverheartbeat_<name>，EX 30s 表示实例存活
#define SERVER_HEARTBEAT_RENEW_SECONDS 10           // 存活 key 的续期间隔（秒），必须显著小于 TTL，留出容错

#define USER_OFFLINE_MESSAGE_STREAM "offlinestream_" // 用户离线消息队列 + uid
#define GROUP_OFFLINE_MESSAGE_STREAM "offlinestream_group_" // 群聊用户离线消息队列 + uid
#define USER_XGROUP_NAME "userxgroup_" // 用户聊天消息消费组 + uid

#define USER_FRIEND_SET "userfriendset_" // 用户好友集合 + uid --- uid->(uid1,uid2...)

#endif //GATESERVER_REDISMANAGER_BOOST_H
