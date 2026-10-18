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
    Type type;
    std::uint64_t from_uid;
    std::uint64_t to_uid;
    bool is_to_group{false};
    std::string body;
    std::string message_id{};
};
}

#endif //GATESERVER_MESSAGETYPE_H
