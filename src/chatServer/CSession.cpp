#include "CSession.h"

#include <boost/uuid.hpp>

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
    : m_socket(io_context), m_server(server), m_beClose(false), m_beHeadParse(false)
{
    const auto uuid = boost::uuids::random_generator()();
    m_uuid = to_string(uuid);
    m_receiveHeadNode = std::make_shared<MessageNode>(HEAD_TOTAL_LEN);
}

CSession::~CSession()
{
    spdlog::info("[server] session destruct");
}


void CSession::start()
{
    asyncReadHead(HEAD_TOTAL_LEN);
}

std::string CSession::getUuid()
{
    return m_uuid;
}

tcp::socket& CSession::getSocket()
{
    return m_socket;
}

void CSession::asyncReadHead(std::uint8_t head_len)
{
    auto self = shared_from_this();
    asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& error_code, std::size_t bytes_transformed)
        {

        });
}

void CSession::send(const std::string& message, short message_id)
{

}
