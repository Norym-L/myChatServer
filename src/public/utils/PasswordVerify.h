#ifndef GATESERVER_PASSWORDVERIFY_H
#define GATESERVER_PASSWORDVERIFY_H

#include <sodium.h>
#include <stdexcept>
#include <string>

class PasswordVerify
{
public:
    /**
     * argon2id加密密码
     * @param password 密码
     * @return 加密后的字符串
     */
    static std::string hashPassword(const std::string &password)
    {
        char hashed[crypto_pwhash_STRBYTES];

        if (crypto_pwhash_str(
                hashed,
                password.c_str(), password.size(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE,
                crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        {
            throw std::runtime_error("crypto_pwhash_str failed");
        }
        return hashed;
    }

    /**
     * 验证密码
     * @param password 密码
     * @param stored 存储的密码
     * @return true 验证成功 false 验证失败
     */
    static bool verifyPassword(const std::string &password, const std::string &stored)
    {
        return crypto_pwhash_str_verify(
                   stored.c_str(),
                   password.c_str(), password.size()) == 0;
    }
};

#endif //GATESERVER_PASSWORDVERIFY_H
