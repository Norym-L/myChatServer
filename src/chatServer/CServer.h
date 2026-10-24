#ifndef GATESERVER_CSERVER_H
#define GATESERVER_CSERVER_H

#include <unordered_map>
#include <mutex>
#include <boost/asio.hpp>

#include "net/CSession.h"


using boost::asio::ip::tcp;

class CServer
{
public:
    CServer(boost::asio::io_context& io_context, std::uint16_t port, std::string server_name);
    ~CServer();
    void clearSession(const std::string& session_id);

    std::string getSelfServerName() { return m_name; }

private:
    void handleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error_code);
    void startAccept();

    boost::asio::io_context& m_ioContext;
    std::uint16_t m_port;
    tcp::acceptor m_acceptor;
    std::unordered_map<std::string, std::shared_ptr<CSession> > m_sessions;
    std::mutex m_mutex;
    std::string m_name;
};


#endif //GATESERVER_CSERVER_H
