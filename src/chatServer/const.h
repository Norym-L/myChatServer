// #ifndef GATESERVER_CONST_H
// #define GATESERVER_CONST_H
//
// namespace chat
// {
// #define MAX_LENGTH      (1024 * 2)
// #define HEAD_TOTAL_LEN  4 // tcp头部长度
// #define HEAD_ID_LEN     2 // tcp头部 message_id 长度
// #define HEAD_DATA_LEN   2 // tcp头部 message_len 长度
// #define MAX_SEND_QUEUE_LEN 4 // tcp发送队列最大长度
//
// enum class ErrorCode : std::uint16_t
// {
//     SUCCESS              = 0,    // 成功
//     ERROR_JSON           = 1001, // Json解析错误
//     RPC_FAILED           = 1002, // RPC请求失败
//     VERIFY_CODE_EXPIRED  = 1003, // 验证码过期
//     VERIFY_CODE_ERROR    = 1004, // 验证码错误
//     USER_EXIST           = 1005, // 用户已存在
//     PASSWORD_ERROR       = 1006, // 密码错误
//     EMAIL_NOT_MATCH      = 1007, // 邮箱不匹配
//     PASSWORD_UP_FAILED   = 1008, // 密码修改失败
//     PASSWORD_INVALID     = 1009, // 密码无效
//     REGISTER_USER_FAILED = 1010, // 用户注册失败
//     USER_UID_INVALID     = 1011, // 非法用户id
// };
//
// enum MessageType : short
// {
//     MESSAGE_CHAT_LOGIN,
//     MESSAGE_CHAT_LOGIN_RESP,
// };
//
// struct UserInfo
// {
//     std::string token;
//     std::string name;
//     size_t uid;
// };
// }
// #endif //GATESERVER_CONST_H
