#include "LogicSystem.h"
#include "HttpConnection.h"
#include "../verifyClient/VerifyGrpcClient.h"
#include <memory>

#include "RedisManager.h"

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

    regPost("/get_Verifycode",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << std::endl;
                connection->m_response.set(http::field::content_type, "text/json");
                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success |
                    !src_root.isMember("email"))
                {
                    std::cout << "Failed to parse Json data!" << std::endl;
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto rsp = VerifyGrpcClient::GetInstance().getverifyCode(email);
                std::cout << "email is: " << email << std::endl;
                root["error"] = rsp.error();
                root["email"] = src_root["email"];
                std::string json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });

    regPost("/user_register",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                std::cout << "receive body is " << body_str << std::endl;
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Reader reader;
                Json::Value root;
                Json::Value src_root;

                if (!reader.parse(body_str, src_root))
                {
                    std::cout << "Failed to parse Json data!" << std::endl;
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
                    std::cout << "password is not equal to confirm" << std::endl;
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                std::string verify_code;
                bool b_get_verify = RedisManager::GetInstance().get(CODE_PREFIX + src_root["email"].asString(), verify_code);

                if (!b_get_verify)
                {
                    std::cout << "get verify code failed" << std::endl;
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }
                if (verify_code != src_root["verify_code"].asString())
                {
                    std::cout << "verify code error: " << verify_code << std::endl;
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                root["error"] = static_cast<int>(ErrorCode::SUCCESS);
                root["email"] = email;
                root["user"] = name;
                root["password"] = password;
                root["confirm"] = confirm;
                root["verify_code"] = src_root["verify_code"].asString();
                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
}
