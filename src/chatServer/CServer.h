#ifndef GATESERVER_CSERVER_H
#define GATESERVER_CSERVER_H

#include <map>
#include <mutex>
#include <boost/asio.hpp>

#include "CSession.h"

namespace chat
{
using boost::asio::ip::tcp;

class CServer
{
public:
    CServer(boost::asio::io_context& io_context, short port);
    ~CServer();
    void clearSession(const std::string& uuid);

private:
    void handleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error_code);
    void startAccept();

    boost::asio::io_context& m_ioContext;
    short m_port;
    tcp::acceptor m_acceptor;
    std::map<std::string, std::shared_ptr<CSession> > m_sessions;
    std::mutex m_mutex;
};
}

#endif //GATESERVER_CSERVER_H
