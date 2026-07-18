#pragma once

#include <bike/protocol.hpp>

#include <asio.hpp>
#include <memory>
#include <string>
#include <vector>

#include "server/router.hpp"

namespace bike::server {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::ip::tcp::socket socket, Router& router, Ctx& ctx);
    void start();
private:
    void do_read_header();
    void do_read_body(std::uint32_t len);
    void do_write(std::vector<std::uint8_t> bytes);

    asio::ip::tcp::socket socket_;
    Router& router_;
    Ctx& ctx_;
    std::uint8_t header_buf_[bike::kHeaderLen];
    std::string body_buf_;
};

} // namespace bike::server
