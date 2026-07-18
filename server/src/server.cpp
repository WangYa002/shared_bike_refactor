#include "server/server.hpp"

namespace bike::server {

Server::Server(asio::io_context& ioc, const std::string& host, int port,
               Router& router, Ctx& ctx)
    : acceptor_(ioc,
                asio::ip::tcp::endpoint(asio::ip::make_address(host),
                                        static_cast<unsigned short>(port))),
      router_(router), ctx_(ctx) {
    do_accept();
}

void Server::do_accept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock,
        [this, sock](std::error_code ec) {
            if (!ec) {
                auto session = std::make_shared<Session>(std::move(*sock), router_, ctx_);
                session->start();
            }
            do_accept();
        });
}

} // namespace bike::server
