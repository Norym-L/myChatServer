#ifndef GATESERVER_CSESSION_H
#define GATESERVER_CSESSION_H

#include <string>
#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include "CServer.h"
#include "MessageNode.h"


class CSession : public std::enable_shared_from_this<CSession>
{
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    ~CSession();

    void start();
    [[nodiscard]] std::string getUuid();
    [[nodiscard]] tcp::socket& getSocket();
    void asyncReadHead(std::uint8_t head_len = HEAD_TOTAL_LEN);
    void send(const std::string& message, short message_id);

private:
    tcp::socket m_socket;
    CServer* m_server;
    std::string m_uuid;
    std::shared_ptr<MessageNode> m_receiveHeadNode;
    bool m_beClose;
    bool m_beHeadParse;
};


#endif //GATESERVER_CSESSION_H
