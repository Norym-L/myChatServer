/**
 * @file const.h
 * @brief const
 *
 * @author norym
 * @date 2026/8/31
 */
#ifndef GATESERVER_CONST_H
#define GATESERVER_CONST_H

#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

#include "Singleton.h"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

#endif //GATESERVER_CONST_H
