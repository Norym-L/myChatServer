#include "VerifyGrpcClient.h"

GetVerifyRsp VerifyGrpcClient::getverifyCode(const std::string& email) const
{
    ClientContext context;
    GetVerifyRsp reply;
    GetVerifyReq request;
    request.set_email(email);

    const Status status = m_stub->GetVerifyCode(&context, request, &reply);
    if (status.ok())
    {
        return reply;
    }
    else
    {
        reply.set_error(static_cast<int32_t>(ErrorCode::RPC_FAILED));
        return reply;
    }
}
