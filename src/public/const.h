/**
 * @file const.h
 * @brief const
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_CONST_H
#define GATESERVER_CONST_H

enum ErrorCode
{
    SUCCESS                            = 0,    // 成功
    ERROR_JSON                         = 1001, // Json解析错误
    RPC_FAILED                         = 1002, // RPC请求失败
    VERIFY_CODE_EXPIRED                = 1003, // 验证码过期
    VERIFY_CODE_ERROR                  = 1004, // 验证码错误
    USER_EXIST                         = 1005, // 用户已存在
    PASSWORD_ERROR                     = 1006, // 密码错误
    EMAIL_NOT_MATCH                    = 1007, // 邮箱不匹配
    PASSWORD_UP_FAILED                 = 1008, // 密码修改失败
    PASSWORD_INVALID                   = 1009, // 密码无效
    REGISTER_USER_FAILED               = 1010, // 用户注册失败
    USER_UID_INVALID                   = 1011, // 非法用户id
    USER_TOKEN_INVALID                 = 1012, // 用户token非法
    SERVICE_UNAVAILABLE                = 1013, // 依赖服务不可用（数据库/Redis 连接池超时或不可达）
    USER_NOT_FOUND                     = 1014, // 查询的用户不存在
    CAN_NOT_ADD_YOURSELF_AS_A_FRIEND   = 1015, // 用户不能把自己作为添加好友的对象
    USER_IS_YOUR_FRIEND_BEFOR          = 1016, // 添加好友的对象已经是好友
    CHACK_YOUR_RECEIVED_FRIEND_REQUEST = 1018, // 请检查好友申请列表，对方已经向你发起了好友申请
    USER_OUTLINE                       = 1019, // 用户离线
};

#define MAX_LENGTH      (1024 * 2)
#define HEAD_TOTAL_LEN  4 // tcp头部长度
#define HEAD_ID_LEN     2 // tcp头部 message_id 长度
#define HEAD_DATA_LEN   2 // tcp头部 message_len 长度
#define MAX_SEND_QUEUE_LEN 4 // tcp发送队列最大长度

enum MessageType : std::uint16_t
{
    MESSAGE_CHAT_LOGIN_REQ = 1001,    //登录请求
    MESSAGE_CHAT_LOGIN_RSP,           //登录回包
    MESSAGE_SEARCH_USER_REQ,          //用户搜索请求
    MESSAGE_SEARCH_USER_RSP,          //搜索用户回包
    MESSAGE_ADD_FRIEND_REQ,           //申请添加好友请求
    MESSAGE_ADD_FRIEND_RSP,           //申请添加好友回复
    MESSAGE_SYNC_FRIEND_REQUEST_REQ,  //获取好友申请列表
    MESSAGE_SYNC_FRIEND_REQUEST_RSP,  //获取好友申请列表回包
    MESSAGE_NOTIFY_ADD_FRIEND_REQ,    //通知用户添加好友申请
    MESSAGE_AUTH_FRIEND_REQ,          //认证好友请求
    MESSAGE_AUTH_FRIEND_RSP,          //认证好友回复
    MESSAGE_NOTIFY_AUTH_FRIEND_REQ,   //通知用户认证好友申请
    MESSAGE_TEXT_CHAT_MSG_REQ,        //文本聊天信息请求
    MESSAGE_TEXT_CHAT_MSG_RSP,        //文本聊天信息回复
    MESSAGE_NOTIFY_TEXT_CHAT_MSG_REQ, //通知用户文本聊天信息
    MESSAGE_HEART_BEAT_REQ,           //心跳请求
    MESSAGE_HEART_BEAT_RSP,           //心跳回包
    MESSAGE_CHECK_IS_USER_ONLINE_REQ, //查询用户是否在线请求
    MESSAGE_CHECK_IS_USER_ONLINE_RSP  //查询用户是否在线请求回包
};

#include <fmt/format.h>
#include <grpcpp/support/status.h>

template<>
struct fmt::formatter<grpc::StatusCode> : fmt::formatter<int>
{
    // 复用 int 的 formatter，直接把枚举当整数格式化
    auto format(grpc::StatusCode s, format_context& ctx) const
    {
        return fmt::formatter<int>::format(static_cast<int>(s), ctx);
    }
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

#endif //GATESERVER_CONST_H
