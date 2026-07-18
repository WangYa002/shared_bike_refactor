#pragma once

#include <asio.hpp>
#include <string>

#include "server/router.hpp"
#include "server/session.hpp"

namespace bike::server {

class Server {
public:
    Server(asio::io_context& ioc, const std::string& host, int port,
           Router& router, Ctx& ctx);
    void run();
private:
    void do_accept();
    asio::ip::tcp::acceptor acceptor_;
    Router& router_;
    Ctx& ctx_;
};

} // namespace bike::server
