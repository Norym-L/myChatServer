#include "../src/utils/PasswordVerify.h"
#include <iostream>
#include <spdlog/spdlog.h>

int main()
{
    std::string stored = PasswordVerify::hashPassword("123456");
    spdlog::info("stored = {}", stored);

    spdlog::info("verify result: {}", PasswordVerify::verifyPassword("123456", stored)); // 1
    spdlog::info("verify result: {}", PasswordVerify::verifyPassword("wrong", stored)); // 0
}