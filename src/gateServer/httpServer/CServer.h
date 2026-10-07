/**
 * @file CServer.h
 * @brief CServer
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_CSERVER_H
#define GATESERVER_CSERVER_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>

namespace  net = boost::asio;
using tcp = boost::asio::ip::tcp;

class CServer : public std::enable_shared_from_this<CServer>
{
public:
    CServer(net::io_context &ioc, const unsigned short &port);
    void start();

private:
    tcp::acceptor m_acceptor;
    net::io_context &m_ioc;
};


#endif //GATESERVER_CSERVER_H
