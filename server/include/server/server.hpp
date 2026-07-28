#pragma once

#include <asio.hpp>
#include <string>

#include "server/router.hpp"
#include "server/session.hpp"
#include "server/util/thread_pool.hpp"

namespace bike::server {

class Server {
public:
    Server(asio::io_context& ioc, const std::string& host, int port,
           Router& router, Ctx& ctx, ThreadPool& pool);
    void run();
private:
    void do_accept();
    asio::ip::tcp::acceptor acceptor_;
    Router& router_;
    Ctx& ctx_;
    ThreadPool& pool_;
};

} // namespace bike::server
