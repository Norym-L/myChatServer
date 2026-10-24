#ifndef GATESERVER_MESSAGETYPE_H
#define GATESERVER_MESSAGETYPE_H
#include <cstdint>
#include <string>

namespace chat_message
{
enum Type
{
    TEXT    = 1, // 文本
    PICTURE = 2, // 图片
    FILE    = 3, // 文件
    EMOJI   = 4, // 表情包
};

struct Message
{
    Type type{};
    std::uint64_t from_uid{0}; // 发送方 uid
    std::uint64_t to_uid{0};   // 接收方 uid，群聊发送时为0，解析后填充
    std::uint64_t group_id{0}; // 群id，单聊为0
    bool group_checked{false}; // 用于标记是否进行分流 (RedisManager::sendMessage)
    std::string body;
    std::string message_id{};
};
}

#endif //GATESERVER_MESSAGETYPE_H
