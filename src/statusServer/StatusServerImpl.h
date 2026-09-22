#ifndef GATESERVER_STATUSSERVERIMPL_H
#define GATESERVER_STATUSSERVERIMPL_H

#include <grpcpp/grpcpp.h>
#include <message.grpc.pb.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;

struct ChatServer
{
    std::string host;
    std::string port;
    std::string name;
    size_t connection_count;
};

class StatusServerImpl final : public StatusService::Service
{
public:
    StatusServerImpl();

    Status GetChatServer(grpc::ServerContext *context, const message::GetChatServerReq *request, message::GetChatServerRsp *response) override;
    Status Login(grpc::ServerContext* context, const message::LoginReq* request, message::LoginRsp* response) override;

private:
    void insertToken(size_t uid, std::string token);
    ChatServer getChatServer();

    std::unordered_map<std::string, ChatServer> m_servers;
    std::unordered_map<size_t, std::string> m_tokens;
    std::mutex m_serverMutex;
    std::mutex m_tokenMutex;
};


#endif //GATESERVER_STATUSSERVERIMPL_H
