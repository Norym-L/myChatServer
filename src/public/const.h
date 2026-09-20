/**
 * @file const.h
 * @brief const
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_CONST_H
#define GATESERVER_CONST_H

#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include "Singleton.h"

namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

enum class ErrorCode
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

struct UserInfo
{
    std::string token;
    std::string name;
    size_t uid;
};


#include <fmt/format.h>
#include <grpcpp/support/status.h>

template<>
struct fmt::formatter<grpc::StatusCode> : fmt::formatter<int>
{
    // 复用 int 的 formatter，直接把枚举当整数格式化
    auto format(grpc::StatusCode s, format_context &ctx) const
    {
        return fmt::formatter<int>::format(static_cast<int>(s), ctx);
    }
};

#define CODE_PREFIX "code_"

#endif //GATESERVER_CONST_H
