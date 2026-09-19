#ifndef GATESERVER_VERIFYGRPCCLIENT_H
#define GATESERVER_VERIFYGRPCCLIENT_H

#include <grpcpp/grpcpp.h>
#include <message.grpc.pb.h>

#include "RPCConnectPool.h"
#include <const.h>
#include <Singleton.h>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVerifyReq;
using message::GetVerifyRsp;
using message::VerifyService;

class VerifyGrpcClient : public TSingLeton<VerifyGrpcClient>
{
    friend class TSingLeton;

public:
    [[nodiscard]] GetVerifyRsp getverifyCode(const std::string &email) const;

private:
    VerifyGrpcClient();

    std::unique_ptr<RPCConnectPool> m_pool;
};


#endif //GATESERVER_VERIFYGRPCCLIENT_H
