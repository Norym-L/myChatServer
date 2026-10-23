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
    REQUEST_VALUE_INVALID              = 1020, // 客户端请求数据非法
    DOUBLE_HANDLE_REQUEST              = 1021, // 重复处理请求
    NOT_FRIEND                         = 1022, // 不是好友关系
    GROUP_ID_INVALID                   = 1023, // 群id非法
    NO_POWER                           = 1024, // 权限不足
    GROUP_FULLED                       = 1025, // 群已满
};

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
    MESSAGE_NOTIFY_ADD_FRIEND_REQ,    //通知用户添加好友申请（收到对方发来的好友申请）
    MESSAGE_AUTH_FRIEND_REQ,          //认证好友请求
    MESSAGE_AUTH_FRIEND_RSP,          //认证好友回复
    MESSAGE_NOTIFY_AUTH_FRIEND_REQ,   //通知用户认证好友申请（收到发送给对方的好友申请处理结果）
    MESSAGE_TEXT_CHAT_MSG_REQ,        //文本聊天信息请求
    MESSAGE_TEXT_CHAT_MSG_RSP,        //文本聊天信息回复
    MESSAGE_NOTIFY_CHAT_MSG_REQ,      //通知用户收到聊天信息
    MESSAGE_HEART_BEAT_REQ,           //心跳请求
    MESSAGE_HEART_BEAT_RSP,           //心跳回包
    MESSAGE_CHECK_IS_USER_ONLINE_REQ, //查询用户是否在线请求
    MESSAGE_CHECK_IS_USER_ONLINE_RSP, //查询用户是否在线请求回包
    MESSAGE_SYNC_FRIEND_LIST_REQ,     //获取好友列表
    MESSAGE_SYNC_FRIEND_LIST_RSP,     //获取好友列表回包
    MESSAGE_NOTIFY_RECEIVE_MESSAGE,   //通知客户端有新的消息
    MESSAGE_SYNC_CHAT_MESSAGE_REQ,    //同步、获取接收聊天消息
    MESSAGE_SYNC_CHAT_MESSAGE_RSP,    //同步、获取接收聊天消息回包
    MESSAGE_ACK_CHAT_MESSAGE_REQ,     //客户端确认收到消息
    MESSAGE_ACK_CHAT_MESSAGE_RSP,     //客户端确认收到消息回包
    MESSAGE_DEL_FRIEDN_REQ,           //删除好友请求
    MESSAGE_DEL_FRIEDN_RSP,           //删除好友回包
    MESSAGE_SET_FRIEND_REMARK_REQ,    //设置好友备注
    MESSAGE_SET_FRIEND_REMARK_RSP,    //设置好友备注回包
    MESSAGE_CHANGE_USER_INFO_REQ,     //编辑用户资料请求
    MESSAGE_CHANGE_USER_INFO_RSP,     //编辑用户资料回包
    MESSAGE_GROUP_CREATE_REQ,         //创建群聊
    MESSAGE_GROUP_CREATE_RSP,         //创建群聊回包
    MESSAGE_GROUP_ADD_MEMBERS_REQ,    //邀请用户加入群聊请求
    MESSAGE_GROUP_ADD_MEMBERS_RSP,    //邀请用户加入群聊回包
    MESSAGE_GROUP_KICK_MEMBER_REQ,    //群聊踢人请求
    MESSAGE_GROUP_KICK_MEMBER_RSP,    //群聊踢人回包
    MESSAGE_GROUP_DISMISS_REQ,        //解散群聊请求
    MESSAGE_GROUP_DISMISS_RSP,        //解散群聊回包
    MESSAGE_GET_GROUP_USER_INFO_REQ,  //获取群成员信息
    MESSAGE_GET_GROUP_USER_INFO_RSP,  //获取群成员信息回包
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

#endif //GATESERVER_CONST_H
