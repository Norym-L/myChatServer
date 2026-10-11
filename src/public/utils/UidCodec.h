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
#include <cstdint>
#include <string_view>

/// 约定：客户端 JSON 中 uid 一律为字符串，服务端内部一律为 std::uint64_t
namespace uid_codec
{
/**
 * 把客户端传来的 uid 字符串解析为 std::uint64_t
 * @param text 客户端 JSON 中 uid 字段的字符串值
 * @param uid 解析结果，仅在返回 true 时被写入
 * @return true 解析成功；false 空串、含非数字字符或数值溢出
 */
[[nodiscard]] inline bool ParseUid(const std::string_view text, std::uint64_t& uid) noexcept
{
    if (text.empty())
    {
        return false;
    }

    // from_chars 不跳过前导空白、不接受正负号，正好拒绝 " 1" / "+1" / "-1" 这类脏数据
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), uid);
    return ec == std::errc() && ptr == text.data() + text.size();
}
}

#endif //GATESERVER_UIDCODEC_H
