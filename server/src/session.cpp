#include "server/session.hpp"
#include "server/logging.hpp"

namespace bike::server {

Session::Session(asio::ip::tcp::socket socket, Router& router, Ctx& ctx, ThreadPool& pool)
    : socket_(std::move(socket)), router_(router), ctx_(ctx), pool_(pool) {}

void Session::start() { do_read_header(); }

void Session::do_read_header() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(header_buf_, bike::kHeaderLen),
        [this, self](std::error_code ec, std::size_t /*n*/) {
            if (ec) {
                BIKE_LOG_DEBUG("session closed: {}", ec.message());
                return;
            }
            if (std::memcmp(header_buf_, bike::kFrameMagic, 4) != 0) {
                BIKE_LOG_WARN("bad magic from client, closing");
                return;
            }
            std::uint16_t eid = static_cast<std::uint16_t>(header_buf_[4]) |
                                (static_cast<std::uint16_t>(header_buf_[5]) << 8);
            std::int32_t len = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(header_buf_[6]) |
                (static_cast<std::uint32_t>(header_buf_[7]) << 8) |
                (static_cast<std::uint32_t>(header_buf_[8]) << 16) |
                (static_cast<std::uint32_t>(header_buf_[9]) << 24));
            if (len < 0 || static_cast<std::uint32_t>(len) > bike::kMaxMessageLen) {
                BIKE_LOG_WARN("bad frame length {}, closing", len);
                return;
            }
            body_buf_.assign(static_cast<std::size_t>(len), '\0');
            (void)eid;
            do_read_body(static_cast<std::uint32_t>(len));
        });
}

void Session::do_read_body(std::uint32_t len) {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(body_buf_.data(), len),
        [this, self](std::error_code ec, std::size_t /*n*/) {
            if (ec) {
                BIKE_LOG_DEBUG("body read failed: {}", ec.message());
                return;
            }
            std::uint16_t eid = static_cast<std::uint16_t>(header_buf_[4]) |
                                (static_cast<std::uint16_t>(header_buf_[5]) << 8);
            // Half-sync/half-async: 把 dispatch(含同步 hiredis/mysqlclient 调用)
            // 投递到业务线程池,避免阻塞 io worker. 完成后再切回 io 线程 async_write.
            pool_.post([self, this, eid, payload = body_buf_]() mutable {
                std::vector<std::uint8_t> out;
                try {
                    out = self->router_.dispatch(eid, payload, self->ctx_);
                } catch (const std::exception& e) {
                    BIKE_LOG_ERROR("dispatch eid={} threw: {}", eid, e.what());
                }
                asio::post(self->socket_.get_executor(),
                    [self, this, out = std::move(out), eid]() mutable {
                        if (out.empty()) {
                            BIKE_LOG_WARN("no handler for eid={}", eid);
                            // 继续读下一帧,保持连接活跃
                            self->do_read_header();
                            return;
                        }
                        self->do_write(std::move(out));
                    });
            });
        });
}

void Session::do_write(std::vector<std::uint8_t> bytes) {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
    asio::async_write(socket_, asio::buffer(*buf),
        [this, self, buf](std::error_code ec, std::size_t /*n*/) {
            if (ec) {
                BIKE_LOG_DEBUG("write failed: {}", ec.message());
                return;
            }
            do_read_header();
        });
}

} // namespace bike::server
