/**
 * @file UrlCodec.h
 * @brief URL 百分号编解码工具
 *
 * @author norym
 * @date 2026/9/22
 */
#ifndef GATESERVER_URLCODEC_H
#define GATESERVER_URLCODEC_H

#include <cctype>
#include <string>

/// URL 编解码：从 HttpConnection.cpp 的匿名命名空间抽出为公共工具，便于单元测试
namespace url_codec
{
/// 10 -> 16：把 0-15 转成对应十六进制字符
inline unsigned char ToHex(const unsigned char x)
{
    return x > 9 ? x + 55 : x + 48;
}

/// 是否为合法十六进制字符
inline bool IsHexDigit(const unsigned char x)
{
    return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F');
}

/// 16 -> 10：调用方须先用 IsHexDigit 过滤，非法输入返回 0
inline unsigned char FromHex(const unsigned char x)
{
    if (x >= 'A' && x <= 'F') return x - 'A' + 10;
    if (x >= 'a' && x <= 'f') return x - 'a' + 10;
    if (x >= '0' && x <= '9') return x - '0';
    return 0;
}

/// URL 编码
inline std::string UrlEncode(const std::string &str)
{
    std::string str_temp;
    const auto length = str.length();
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto ch = static_cast<unsigned char>(str[i]);
        if (std::isalnum(ch) || // 数字、字母、符号
            str[i] == '-' ||
            str[i] == '_' ||
            str[i] == '.' ||
            str[i] == '~')
        {
            str_temp += str[i];
        }
        else if (str[i] == ' ') // 空
        {
            str_temp += '+';
        }
        else // 特殊字符 加‘%’且高四位和第四位转16进制
        {
            str_temp += '%';
            str_temp += static_cast<char>(ToHex(ch >> 4));
            str_temp += static_cast<char>(ToHex(ch & 0x0F));
        }
    }
    return str_temp;
}

/// URL 解码
inline std::string UrlDecode(const std::string &str)
{
    std::string str_temp;
    const auto length = str.length();
    for (std::size_t i = 0; i < length; ++i)
    {
        if (str[i] == '+')
        {
            str_temp += ' ';
        }
        else if (str[i] == '%')
        {
            // 百分号后不足 2 个字符、或不是合法十六进制时，原样保留 '%' 并继续，
            // 既避免越界读取，也避免把非法转义解成错误字节
            if (i + 2 >= length ||
                !IsHexDigit(static_cast<unsigned char>(str[i + 1])) ||
                !IsHexDigit(static_cast<unsigned char>(str[i + 2])))
            {
                str_temp += '%';
                continue;
            }
            const auto high = FromHex(static_cast<unsigned char>(str[++i]));
            const auto low = FromHex(static_cast<unsigned char>(str[++i]));
            str_temp += static_cast<char>(high << 4 | low);
        }
        else
        {
            str_temp += str[i];
        }
    }
    return str_temp;
}
}

#endif //GATESERVER_URLCODEC_H
