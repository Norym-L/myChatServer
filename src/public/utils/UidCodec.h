/**
 * @file UidCodec.h
 * @brief 客户端 JSON 中的 uid（字符串）与服务端内部 std::uint64_t 之间的转换工具
 *
 * @author norym
 * @date 2026/10/11
 */
#ifndef GATESERVER_UIDCODEC_H
#define GATESERVER_UIDCODEC_H

#include <charconv>
#include <string_view>
#include <concepts>
#include <type_traits>

namespace uid_codec
{
template<typename T>
concept FromCharsParsable =
    requires(const char* first, const char* last, T& value)
    {
        {
            std::from_chars(first, last, value)
        }
        -> std::same_as<std::from_chars_result>;
    };

template<FromCharsParsable T>
[[nodiscard]] bool ParseNumber(const std::string_view text, T& int_num) noexcept
{
    if (text.empty())
    {
        return false;
    }

    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), int_num);
    return ec == std::errc() && ptr == text.data() + text.size();
}

[[nodiscard]] inline bool ParseUid(const std::string_view text, std::uint64_t& uid) noexcept
{
    return ParseNumber(text, uid);
}
}

#endif //GATESERVER_UIDCODEC_H
