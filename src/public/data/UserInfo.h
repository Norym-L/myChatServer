#ifndef GATESERVER_USERINFO_H
#define GATESERVER_USERINFO_H

#include <string>
#include <cstdint>
#include <utility>

enum Sex : unsigned char
{
    MALE,
    FEMALE,
    UNKNOWN
};

enum FriendApplyStatus : unsigned char // 是否接受
{
    WAITING,  // 等待处理
    ACCEPTED, // 已经接受
    REFUSEED, // 拒绝
};

struct UserInfo
{
    UserInfo(std::string token, std::string name, std::string email, std::string desc, std::string icon, const std::uint64_t uid, const Sex sex)
        : token(std::move(token)), name(std::move(name)), email(std::move(email)), desc(std::move(desc)), icon(std::move(icon)), uid(uid), sex(sex)
    {}

    std::string token;
    std::string name;
    std::string email;
    std::string desc; // 个性签名
    std::string icon;
    std::uint64_t uid;
    Sex sex;
};

struct ApplyInfo
{
    ApplyInfo(std::string name, std::string desc, std::string nick, const std::uint64_t uid, const FriendApplyStatus status)
        : name(std::move(name)),
          desc(std::move(desc)),
          nick(std::move(nick)),
          uid(uid),
          status(status)
    {}

    std::string name;
    std::string desc; // 个性签名
    std::string nick;
    std::uint64_t uid;
    FriendApplyStatus status;
};


#endif //GATESERVER_USERINFO_H
