#include "../src/utils/PasswordVerify.h"
#include <iostream>

int main()
{
    std::string stored = PasswordVerify::hashPassword("123456");
    std::cout << "stored = " << stored << "\n";

    std::cout << PasswordVerify::verifyPassword("123456", stored) << "\n"; // 1
    std::cout << PasswordVerify::verifyPassword("wrong",  stored) << "\n"; // 0
}