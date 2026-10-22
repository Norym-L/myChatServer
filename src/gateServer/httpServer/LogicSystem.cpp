#include "LogicSystem.h"
#include "HttpConnection.h"
#include "../verifyClient/VerifyGrpcClient.h"
#include <memory>
#include <optional>
#include <utility>
#include <spdlog/spdlog.h>
#include <json/value.h>
#include <json/reader.h>


#include <RedisManager.h>
#include <sql/mysql/MySqlManager.h>
#include <utils/UidCodec.h>
#include "../statusClient/StatusGrpcClient.h"

namespace net = boost::asio;

LogicSystem::~LogicSystem() = default;

net::awaitable<bool> LogicSystem::handleGet(std::string path, std::shared_ptr<HttpConnection> connection)
{
    const auto it = m_get_handlers.find(path);
    if (it == m_get_handlers.end())
    {
        co_return false;
    }
    co_await it->second(std::move(connection));
    co_return true;
}

net::awaitable<bool> LogicSystem::handlePost(std::string path, std::shared_ptr<HttpConnection> connection)
{
    const auto it = m_post_handlers.find(path);
    if (it == m_post_handlers.end())
    {
        co_return false;
    }
    co_await it->second(std::move(connection));
    co_return true;
}

void LogicSystem::regGet(std::string url, HttpHandler handler)
{
    m_get_handlers.insert(std::make_pair(std::move(url), std::move(handler)));
}

void LogicSystem::regPost(std::string url, HttpHandler handler)
{
    m_post_handlers.insert(std::make_pair(std::move(url), std::move(handler)));
}

LogicSystem::LogicSystem()
{
    regGet("/get_test",
           [](const std::shared_ptr<HttpConnection>& connection) -> net::awaitable<void>
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
               co_return;
               });
    /// 获取验证码
    regPost("/get_Verifycode",
            [](std::shared_ptr<HttpConnection> connection) -> net::awaitable<void>
                {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                SPDLOG_DEBUG("[LogicSystem] receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");
                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                if (!parse_success ||
                    !src_root.isMember("email"))
                {
                    SPDLOG_DEBUG("[LogicSystem] Failed to parse Json data!");
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto rsp = VerifyGrpcClient::GetInstance().getverifyCode(email);
                SPDLOG_INFO("[LogicSystem] email is: {}", email);
                root["error"] = rsp.error();
                root["email"] = src_root["email"];
#ifdef GATE_DEBUG
                // debug模式下，获取验证码额外返回验证码
                std::string verify_code;
                try
                {
                    verify_code = (co_await RedisManager::GetInstance().get(CODE_PREFIX + email))
                       .value_or("there is no verify in redis!");
                }
                catch (const std::exception& e)
                {
                    SPDLOG_ERROR("[LogicSystem] /get_Verifycode get code from redis failed: {}", e.what());
                    verify_code = "redis unavailable";
                }
                root["verify_code"] = verify_code;
#endif

                std::string json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                co_return;
                });
    /// 用户注册
    regPost("/user_register",
            [](std::shared_ptr<HttpConnection> connection) -> net::awaitable<void>
                {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                SPDLOG_DEBUG("[LogicSystem] /user_register receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Reader reader;
                Json::Value root;
                Json::Value src_root;

                if (!reader.parse(body_str, src_root) ||
                    !src_root.isMember("email") ||
                    !src_root.isMember("user") ||
                    !src_root.isMember("password") ||
                    !src_root.isMember("confirm") ||
                    !src_root.isMember("verify_code"))
                {
                    // 必填字段缺失时返回 JSON 错误，避免对缺失字段静默取空值
                    SPDLOG_DEBUG("[LogicSystem] /user_register Failed to parse Json or missing required fields!");
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return; // 只是参数不对，不需要[断开连接]更严重false
                }

                auto email = src_root["email"].asString();
                auto name = src_root["user"].asString();
                auto password = src_root["password"].asString();
                auto confirm = src_root["confirm"].asString();
                // 密码不一致
                if (password != confirm)
                {
                    SPDLOG_DEBUG("[LogicSystem] /user_register password is not equal to confirm");
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return; // 只是参数不对，不需要[断开连接]更严重false
                }

                std::optional<std::string> verify_code;
                try
                {
                    verify_code = co_await RedisManager::GetInstance().get(CODE_PREFIX + email);
                }
                catch (const std::exception& e)
                {
                    // Redis 不可用与验证码过期是两类问题，需返回不同错误码
                    SPDLOG_ERROR("[LogicSystem] /user_register get verify code failed email: {}, error: {}", email, e.what());
                    root["error"] = static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE);
                    beast::ostream(connection->m_response.body()) << root.toStyledString();
                    co_return;
                }

                if (!verify_code)
                {
                    SPDLOG_DEBUG("[LogicSystem] /user_register get verify code failed email: {}", email);
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return; // 只是参数不对，不需要[断开连接]更严重false
                }
                if (*verify_code != src_root["verify_code"].asString())
                {
                    SPDLOG_DEBUG("[LogicSystem] /user_register verify code error verify is: {}, email: {}", *verify_code, email);
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return; // 只是参数不对，不需要[断开连接]更严重false
                }

                //注册用户
                int uid = MySqlManager::GetInstance().registerUser(email, name, password);
                // <= 0 一律算失败：只判 -1 会把 DAO 的 -11（数据库异常）当成 uid 回显给客户端
                if (uid <= 0)
                {
                    SPDLOG_WARN("[LogicSystem] /user_register register user email: {} failed, ret: {}", email, uid);
                    root["error"] = static_cast<int>(ErrorCode::REGISTER_USER_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                SPDLOG_INFO("[LogicSystem] /user_register user register success, uid: {}, email: {}", uid, email);

                root["error"] = static_cast<int>(ErrorCode::SUCCESS);
                root["uid"] = std::to_string(uid); // uid 对外统一为字符串
                root["email"] = email;
                root["user"] = name;
                // 响应不回显明文密码
                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                co_return;
                });
    /// 用户重置密码
    regPost("/reset_password",
            [](std::shared_ptr<HttpConnection> connection) -> net::awaitable<void>
                {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                SPDLOG_DEBUG("[LogicSystem] /reset_password receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                // 必填字段缺失时返回 JSON 错误，避免对缺失字段静默取空值
                if (!parse_success ||
                    !src_root.isMember("user") ||
                    !src_root.isMember("email") ||
                    !src_root.isMember("password") ||
                    !src_root.isMember("verify_code"))
                {
                    SPDLOG_DEBUG("[LogicSystem] /reset_password Failed to parse JSON or missing required fields!");
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return;
                }

                // 客户端 uid 统一以字符串传递，这里解析为 std::uint64_t
                const auto account_str = src_root["user"].asString();
                std::uint64_t account{0};
                if (!uid_codec::ParseUid(account_str, account))
                {
                    SPDLOG_DEBUG("[LogicSystem] /reset_password invalid uid string: {}", account_str);
                    root["error"] = static_cast<int>(ErrorCode::USER_UID_INVALID);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return;
                }
                auto email = src_root["email"].asString();
                auto password = src_root["password"].asString();

                std::optional<std::string> verify_code;
                try
                {
                    verify_code = co_await RedisManager::GetInstance().get(CODE_PREFIX + email);
                }
                catch (const std::exception& e)
                {
                    SPDLOG_ERROR("[LogicSystem] /reset_password get verify code failed email: {}, error: {}", email, e.what());
                    root["error"] = static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE);
                    beast::ostream(connection->m_response.body()) << root.toStyledString();
                    co_return;
                }

                if (!verify_code)
                {
                    SPDLOG_DEBUG("[LogicSystem] /reset_password get verify code expired");
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_EXPIRED);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return;
                }

                if (*verify_code != src_root["verify_code"].asString())
                {
                    SPDLOG_DEBUG("[LogicSystem] /reset_password verify code not match");
                    root["error"] = static_cast<int>(ErrorCode::VERIFY_CODE_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                const auto accountValid = MySqlManager::GetInstance().checkIsAccountMatchEmail(account, email);
                // 依赖不可用（数据库超时/异常）与账号邮箱不匹配是两类问题，需返回不同错误码
                if (accountValid == MatchResult::Unavailable)
                {
                    SPDLOG_ERROR("[LogicSystem] /reset_password account check unavailable, uid: {}", account);
                    root["error"] = static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }
                if (accountValid == MatchResult::NotMatched)
                {
                    SPDLOG_DEBUG("[LogicSystem] /reset_password check account match email failed");
                    root["error"] = static_cast<int>(ErrorCode::EMAIL_NOT_MATCH);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                //更新密码为最新密码
                bool b_up = MySqlManager::GetInstance().updatePassword(account, password);
                if (!b_up)
                {
                    SPDLOG_ERROR("[LogicSystem] /reset_password update password failed uid: {}", account);
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_UP_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                SPDLOG_INFO("[LogicSystem] /reset_password succeed to update password uid: {}", account);
                root["error"] = 0;
                root["email"] = email;
                // 响应不回显明文密码；请求字段名为 verify_code
                root["verify_code"] = src_root["verify_code"].asString();
                std::string jsonstr = root.toStyledString();
                beast::ostream(connection->m_response.body()) << jsonstr;
                co_return;
                });
    /// 用户登录
    regPost("/user_login",
            [](std::shared_ptr<HttpConnection> connection) -> net::awaitable<void>
                {
                auto body_str = boost::beast::buffers_to_string(connection->m_request.body().data());
                SPDLOG_DEBUG("[LogicSystem] /user_login receive body is {}", body_str);
                connection->m_response.set(http::field::content_type, "text/json");

                Json::Value root;
                Json::Reader reader;
                Json::Value src_root;

                bool parse_success = reader.parse(body_str, src_root);
                // 必填字段缺失时返回 JSON 错误，避免对缺失字段静默取空值
                if (!parse_success ||
                    !src_root.isMember("user") ||
                    !src_root.isMember("password"))
                {
                    SPDLOG_DEBUG("[LogicSystem] /user_login Failed to parse JSON or missing required fields!");
                    root["error"] = static_cast<int>(ErrorCode::ERROR_JSON);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return;
                }

                // 客户端 uid 统一以字符串传递，这里解析为 std::uint64_t
                const auto account_str = src_root["user"].asString();
                std::uint64_t account{0};
                if (!uid_codec::ParseUid(account_str, account))
                {
                    // 空串或非纯数字不能进入后续校验，否则会以 uid=0 误判为"密码错误"
                    SPDLOG_DEBUG("[LogicSystem] /user_login invalid uid string: {}", account_str);
                    root["error"] = static_cast<int>(ErrorCode::USER_UID_INVALID);
                    std::string json_str = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << json_str;
                    co_return;
                }
                auto password = src_root["password"].asString();

                const auto passwordMatch = MySqlManager::GetInstance().checkIsPasswordMatchAccount(account, password);
                // 依赖不可用（数据库超时/异常）与密码错误是两类问题，需返回不同错误码
                if (passwordMatch == MatchResult::Unavailable)
                {
                    SPDLOG_ERROR("[LogicSystem] /user_login password check unavailable, uid: {}", account);
                    root["error"] = static_cast<int>(ErrorCode::SERVICE_UNAVAILABLE);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }
                if (passwordMatch == MatchResult::NotMatched)
                {
                    SPDLOG_DEBUG("[LogicSystem] /user_login check password failed");
                    root["error"] = static_cast<int>(ErrorCode::PASSWORD_ERROR);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                //查询StatusServer找到合适的连接
                auto reply = StatusGrpcClient::GetInstance().getChatServer(account);
                if (reply.error())
                {
                    SPDLOG_ERROR("[LogicSystem] /user_login grpc get chat server failed, error is {}", reply.error());
                    root["error"] = static_cast<int>(ErrorCode::RPC_FAILED);
                    std::string jsonstr = root.toStyledString();
                    beast::ostream(connection->m_response.body()) << jsonstr;
                    co_return;
                }

                SPDLOG_INFO("[LogicSystem] /user_login succeed login user uid: {}", account);

                root["error"] = 0;
                root["email"] = src_root["email"].asString();
                root["uid"] = std::to_string(account); // uid 对外统一为字符串
                root["token"] = reply.token();
                root["host"] = reply.host();
                root["port"] = reply.port();
                auto json_str = root.toStyledString();
                beast::ostream(connection->m_response.body()) << json_str;
                co_return;
                });
}
