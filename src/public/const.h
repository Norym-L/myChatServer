/**
 * @file const.h
 * @brief const
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_CONST_H
#define GATESERVER_CONST_H

#include <string>
#include <cstddef>

enum ErrorCode
{
    SUCCESS              = 0,    // 成功
    ERROR_JSON           = 1001, // Json解析错误
    RPC_FAILED           = 1002, // RPC请求失败
    VERIFY_CODE_EXPIRED  = 1003, // 验证码过期
    VERIFY_CODE_ERROR    = 1004, // 验证码错误
    USER_EXIST           = 1005, // 用户已存在
    PASSWORD_ERROR       = 1006, // 密码错误
    EMAIL_NOT_MATCH      = 1007, // 邮箱不匹配
    PASSWORD_UP_FAILED   = 1008, // 密码修改失败
    PASSWORD_INVALID     = 1009, // 密码无效
    REGISTER_USER_FAILED = 1010, // 用户注册失败
    USER_UID_INVALID     = 1011, // 非法用户id
    USER_TOKEN_INVALID   = 1012, // 用户token非法
    SERVICE_UNAVAILABLE  = 1013, // 依赖服务不可用（数据库/Redis 连接池超时或不可达）
};

/// 账号/密码校验结果：区分"业务不匹配"与"依赖不可用"，避免两者共用同一错误码
enum class MatchResult
{
    Matched,     // 匹配
    NotMatched,  // 不匹配
    Unavailable, // 依赖服务不可用（连接池获取超时、数据库异常等）
};


#define MAX_LENGTH      (1024 * 2)
#define HEAD_TOTAL_LEN  4 // tcp头部长度
#define HEAD_ID_LEN     2 // tcp头部 message_id 长度
#define HEAD_DATA_LEN   2 // tcp头部 message_len 长度
#define MAX_SEND_QUEUE_LEN 4 // tcp发送队列最大长度

enum MessageType : short
{
    MESSAGE_CHAT_LOGIN,
    MESSAGE_CHAT_LOGIN_RESP,
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
#define LOGIN_COUNT "logincount_"

#endif //GATESERVER_CONST_H
