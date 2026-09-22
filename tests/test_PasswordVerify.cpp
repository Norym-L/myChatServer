#include "../src/public/utils/PasswordVerify.h"
#include <iostream>
#include <spdlog/spdlog.h>

int main()
{
    std::string stored = PasswordVerify::hashPassword("123456");
    spdlog::info("stored = {}", stored);

    // 断言校验结果，失败时返回非 0
    const bool match = PasswordVerify::verifyPassword("123456", stored);
    const bool mismatch = PasswordVerify::verifyPassword("wrong", stored);
    spdlog::info("verify result: {}", match);
    spdlog::info("verify result: {}", mismatch);

    if (!match || mismatch)
    {
        std::cerr << "PasswordVerify test FAILED" << std::endl;
        return 1;
    }

    std::cout << "PasswordVerify test PASSED" << std::endl;
    return 0;
}
