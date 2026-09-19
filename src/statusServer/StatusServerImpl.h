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
};

class StatusServerImpl : public StatusService::Service
{
public:
    StatusServerImpl();
    Status GetChatServer(grpc::ServerContext *context, const message::GetChatServerReq *request, message::GetChatServerRsp *response) override;

    std::vector<ChatServer> m_servers;
    int m_server_index{};
};


#endif //GATESERVER_STATUSSERVERIMPL_H
