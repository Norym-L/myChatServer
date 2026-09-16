#include "LogicSystem.h"
#include "HttpConnection.h"
#include "../verifyClient/VerifyGrpcClient.h"
#include <memory>

#include "RedisManager.h"
#include "../sql/mysql/MySqlManager.h"

LogicSystem::~LogicSystem() = default;

bool LogicSystem::handleGet(std::string path, std::shared_ptr<HttpConnection> connection)
{
    if (!m_get_handlers.contains(path))
    {
        return false;
    }

    m_get_handlers[path](connection);
    return true;
}

bool LogicSystem::handlePost(std::string path, std::shared_ptr<HttpConnection> connection)
{
    if (!m_post_handlers.contains(path))
    {
        return false;
    }
    m_post_handlers[path](connection);
    return true;
}

void LogicSystem::regGet(std::string url, HttpHandler handler)
{
    m_get_handlers.insert(std::make_pair(url, handler));
}

void LogicSystem::regPost(std::string url, HttpHandler handler)
{
    m_post_handlers.insert(std::make_pair(url, handler));
}

LogicSystem::LogicSystem()
{
    regGet("/get_test",
           [](const std::shared_ptr<HttpConnection> &connection)
           {
               beast::ostream(connection->m_response.body()) << "receive get_test req";
               int i = 0;
               for (auto &element: connection->m_getParams)
               {
                   ++i;
                   beast::ostream(connection->m_response.body())
                           << "param " << i << " key is " << element.first
                           << " value is " << element.second << "\n";
               }
           });
    /// 获取验证码
    regPost("/get_Verifycode",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << "\n";
                connection->m_response.set(http::field::content_type, "text/json");
                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success ||
                    !src_root.isMember("email"))
                {
                    std::cout << "Failed to parse Json data!" << "\n";
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto rsp = VerifyGrpcClient::GetInstance().getverifyCode(email);
                std::cout << "email is: " << email << "\n";
                root["error"] = rsp.error();
                root["email"] = src_root["email"];
                std::string json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
    /// 用户注册
    regPost("/user_register",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << "\n";
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Reader reader;
                Json::Value root;
                Json::Value src_root;

                if (!reader.parse(body_str, src_root))
                {
                    std::cout << "Failed to parse Json data! \n";
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto name = src_root["user"].asString();
                auto password = src_root["password"].asString();
                auto confirm = src_root["confirm"].asString();
                // 密码不一致
                if (password != confirm)
                {
                    std::cout << "password is not equal to confirm \n";
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                std::string verify_code;
                bool b_get_verify = RedisManager::GetInstance().get(CODE_PREFIX + src_root["email"].asString(), verify_code);

                if (!b_get_verify)
                {
                    std::cout << "get verify code failed" << "\n";
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }
                if (verify_code != src_root["verify_code"].asString())
                {
                    std::cout << "verify code error: " << verify_code << "\n";
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                //查找数据库判断用户是否存在
                int uid = MySqlManager::GetInstance().registerUser(email, name, password);
                if (uid == 0 || uid == -1)
                {
                    std::cout << " user or email exist \n";
                    root["error"] = static_cast<int>(ErrorCode::USER_EXIST);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                root["error"] = static_cast<int>(ErrorCode::SUCCESS);
                root["uid"] = uid;
                root["email"] = email;
                root["user"] = name;
                root["password"] = password;
                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
    /// 用户重置密码
    regPost("/reset_password",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << "\n";
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success)
                {
                    std::cout << "Failed to parse JSON data! \n";
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true;
                }

                auto account = src_root["user"].asInt();
                auto email = src_root["email"].asString();
                auto password = src_root["password"].asString();

                std::string verify_code;
                bool b_get_verify = RedisManager::GetInstance().get(CODE_PREFIX + src_root["email"].asString(), verify_code);
                if (!b_get_verify)
                {
                    std::cout << "get verify code expired \n";
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true;
                }

                if (verify_code != src_root["verify_code"].asString())
                {
                    std::cout << " verify code error \n";
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                bool b_accountValid = MySqlManager::GetInstance().checkIsAccountMatchEmail(account, email);
                if (!b_accountValid)
                {
                    std::cout << " check account match email failed \n";
                    root["error"] = static_cast<int>(ErrorCode::EMAIL_NOT_MATCH);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                //更新密码为最新密码
                bool b_up = MySqlManager::GetInstance().updatePassword(account, password);
                if (!b_up)
                {
                    std::cout << " update password failed \n";
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_UP_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                std::cout << "succeed to update password" << password << "\n";
                root["error"] = 0;
                root["email"] = email;
                root["passwd"] = password;
                root["varifycode"] = src_root["varifycode"].asString();
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->m_response.body()) << jsonstr;
                return true;
            });
    /// 用户登录
    regPost("/user_login",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << "\n";
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success)
                {
                    std::cout << "Failed to parse JSON data! \n";
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true;
                }

                auto account = src_root["user"].asInt();
                auto password = src_root["password"].asString();

                bool b_pass_match_success = MySqlManager::GetInstance().chechIsPasswordMatchAccount(account, password);
                if (!b_pass_match_success)
                {
                    std::cout << " check password failed \n";
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                // TODO 状态服务器连接
                std::cout << "succeed login user: " << account << "\n";
                root["error"] = 0;

                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
}
