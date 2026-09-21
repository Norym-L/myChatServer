#include "LogicSystem.h"
#include "HttpConnection.h"
#include "../verifyClient/VerifyGrpcClient.h"
#include <memory>
#include <spdlog/spdlog.h>

#include "RedisManager.h"
#include <sql/mysql/MySqlManager.h>
#include "../statusClient/StatusGrpcClient.h"

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
                spdlog::debug("[LogicSystem] receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");
                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success ||
                    !src_root.isMember("email"))
                {
                    spdlog::debug("[LogicSystem] Failed to parse Json data!");
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto rsp = VerifyGrpcClient::GetInstance().getverifyCode(email);
                spdlog::info("[LogicSystem] email is: {}", email);
                root["error"] = rsp.error();
                root["email"] = src_root["email"];
#ifdef GATE_DEBUG
                // debug模式下，获取验证码额外返回验证码
                std::string verify_code;
                bool b_get_verify = RedisManager::GetInstance().get(CODE_PREFIX + src_root["email"].asString(), verify_code);
                if (!b_get_verify)
                {
                    verify_code = "there is no verify in redis!";
                }
                root["verify_code"] = verify_code;
#endif

                std::string json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
    /// 用户注册
    regPost("/user_register",
            [](std::shared_ptr<HttpConnection> connection)
            {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                spdlog::debug("[LogicSystem] /user_register receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Reader reader;
                Json::Value root;
                Json::Value src_root;

                if (!reader.parse(body_str, src_root))
                {
                    spdlog::debug("[LogicSystem] /user_register Failed to parse Json from client!");
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
                    spdlog::debug("[LogicSystem] /user_register password is not equal to confirm");
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                std::string verify_code;
                bool b_get_verify = RedisManager::GetInstance().get(CODE_PREFIX + src_root["email"].asString(), verify_code);

                if (!b_get_verify)
                {
                    spdlog::debug("[LogicSystem] /user_register get verify code failed email: {}", email);
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }
                if (verify_code != src_root["verify_code"].asString())
                {
                    spdlog::debug("[LogicSystem] /user_register verify code error verify is: {}, email: {}", verify_code, email);
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true; // 只是参数不对，不需要[断开连接]更严重false
                }

                //注册用户
                int uid = MySqlManager::GetInstance().registerUser(email, name, password);
                if (uid == -1)
                {
                    spdlog::debug("[LogicSystem] /user_register register user email: {} failed!", email);
                    root["error"] = static_cast<int>(ErrorCode::REGISTER_USER_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                spdlog::info("[LogicSystem] /user_register user register success, uid: {}, email: {}", uid, email);

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
                spdlog::debug("[LogicSystem] /reset_password receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success)
                {
                    spdlog::debug("[LogicSystem] /reset_password Failed to parse JSON from client!");
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
                    spdlog::debug("[LogicSystem] /reset_password get verify code expired");
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    return true;
                }

                if (verify_code != src_root["verify_code"].asString())
                {
                    spdlog::debug("[LogicSystem] /reset_password verify code not match");
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                bool b_accountValid = MySqlManager::GetInstance().checkIsAccountMatchEmail(account, email);
                if (!b_accountValid)
                {
                    spdlog::debug("[LogicSystem] /reset_password check account match email failed");
                    root["error"] = static_cast<int>(ErrorCode::EMAIL_NOT_MATCH);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                //更新密码为最新密码
                bool b_up = MySqlManager::GetInstance().updatePassword(account, password);
                if (!b_up)
                {
                    spdlog::error("[LogicSystem] /reset_password update password failed uid: {}", account);
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_UP_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                spdlog::info("[LogicSystem] /reset_password succeed to update password uid: {}", account);
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
                spdlog::debug("[LogicSystem] /user_login receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success)
                {
                    spdlog::debug("[LogicSystem] /user_login Failed to parse JSON form client!");
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
                    spdlog::debug("[LogicSystem] /user_login check password failed");
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                //查询StatusServer找到合适的连接
                auto reply = StatusGrpcClient::GetInstance().getChatServer(account);
                if (reply.error())
                {
                    spdlog::error("[LogicSystem] /user_login grpc get chat server failed, error is {}", reply.error());
                    root["error"] = static_cast<int>(ErrorCode::RPC_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    return true;
                }

                spdlog::info("[LogicSystem] /user_login succeed login user uid: {}", account);

                root["error"] = 0;
                root["email"] = src_root["email"].asString();
                root["uid"] = account;
                root["token"] = reply.token();
                root["host"] = reply.host();
                root["port"] = reply.port();
                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                return true;
            });
}
