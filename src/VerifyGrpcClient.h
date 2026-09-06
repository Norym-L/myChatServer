#ifndef GATESERVER_VERIFYGRPCCLIENT_H
#define GATESERVER_VERIFYGRPCCLIENT_H

#include <grpcpp/grpcpp.h>
#include <message.grpc.pb.h>

#include "const.h"
#include "Singleton.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVerifyReq;
using message::GetVerifyRsp;
using message::VerifyServer;

class VerifyGrpcClient : public TSingLeton<VerifyGrpcClient>
{
    friend class TSingLeton;

public:
    [[nodiscard]] GetVerifyRsp getverifyCode(const std::string& email) const;

private:
    VerifyGrpcClient()
    {
        const std::shared_ptr<Channel> channel = grpc::CreateChannel(
        "127.0.0.1:50051",
        grpc::InsecureChannelCredentials());
        m_stub = VerifyServer::NewStub(channel);
    }

    std::unique_ptr<VerifyServer::Stub> m_stub;
};


#endif //GATESERVER_VERIFYGRPCCLIENT_H
