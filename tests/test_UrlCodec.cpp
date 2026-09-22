#include "../src/public/utils/UrlCodec.h"

#include <iostream>
#include <string>

namespace
{
int g_failed = 0;

void check(const std::string &case_name, const std::string &actual, const std::string &expected)
{
    if (actual != expected)
    {
        std::cerr << "[FAIL] " << case_name << " actual=[" << actual << "] expected=[" << expected << "]" << std::endl;
        ++g_failed;
        return;
    }
    std::cout << "[ OK ] " << case_name << std::endl;
}
}

int main()
{
    using url_codec::UrlDecode;
    using url_codec::UrlEncode;

    // 百分号转义必须还原成对应字节，而不是字节的十进制文本
    check("decode %41 -> A", UrlDecode("%41"), "A");
    check("decode %2F -> /", UrlDecode("%2F"), "/");
    check("decode %E4%BD%A0 -> UTF-8 三个字节", UrlDecode("%E4%BD%A0"), "\xE4\xBD\xA0");

    // 空格：'+' 与 '%20' 都应解成空格
    check("decode + -> space", UrlDecode("hello+world"), "hello world");
    check("decode %20 -> space", UrlDecode("hello%20world"), "hello world");

    // 边界：截断或非法的转义按原样保留，且不得越界读取
    check("decode truncated %", UrlDecode("abc%"), "abc%");
    check("decode truncated %4", UrlDecode("abc%4"), "abc%4");
    check("decode invalid %ZZ", UrlDecode("abc%ZZde"), "abc%ZZde");

    // 编码
    check("encode space -> +", UrlEncode("hello world"), "hello+world");
    check("encode / -> %2F", UrlEncode("a/b"), "a%2Fb");
    check("encode unreserved unchanged", UrlEncode("aZ0-_.~"), "aZ0-_.~");

    // 编解码往返
    check("roundtrip", UrlDecode(UrlEncode("k=1 & v=2")), "k=1 & v=2");

    if (g_failed != 0)
    {
        std::cerr << "UrlCodec test FAILED: " << g_failed << " case(s)" << std::endl;
        return 1;
    }
    std::cout << "UrlCodec test PASSED" << std::endl;
    return 0;
}
