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

struct UidAndRemarkInUserFriend
{
    UidAndRemarkInUserFriend(std::string remark, std::uint64_t uid)
        : remark(std::move(remark)), uid(uid)
    {}

    std::string remark{};
    std::uint64_t uid{};
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
    explicit ApplyInfo(const std::uint64_t add_friend_request_id, const std::uint64_t from_uid, const std::uint64_t to_uid, std::string name, std::string desc, std::string icon,
                       std::string nick, const Sex sex, const FriendApplyStatus status)
        : add_friend_request_id(add_friend_request_id), from_uid(from_uid), to_uid(to_uid), name(std::move(name)), desc(std::move(desc)), icon(std::move(icon)), nick(std::move(nick)), sex(sex), status(status)
    {}

    void setIcon(const std::string& head) { icon = head; }

    std::uint64_t add_friend_request_id;
    std::uint64_t from_uid{};
    std::uint64_t to_uid{};
    std::string name{};
    std::string desc{};
    std::string icon{};
    std::string nick{};
    Sex sex{Sex::UNKNOWN};
    FriendApplyStatus status{FriendApplyStatus::WAITING}; // 是否接受
};

struct GroupUserInfo // 群成员信息
{
    std::string nick_g;   // 在群里的备注
    std::string joinedAt; // 时间戳，用户加群时间
    std::uint64_t uid;
    std::uint8_t role; // 0 普通成员，1 管理员， 2 群主
};


#endif //GATESERVER_USERINFO_H
