// InProcessRouterSink 测试: 进程内直连 Router 的 Step 2 stub,
// 验证 seq 回带链路(handler 编码 seq=0 → 网关 stamp → decode 还原)。

#include "server/gateway/packet_sink.hpp"

#include <bike/protocol.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace bike;
using namespace bike::gateway;
using bike::server::Ctx;
using bike::server::Router;

namespace {
// 模拟 handler 的行为: 自行 encode 完整响应帧(不感知 seq)
std::vector<std::uint8_t> echo_handler(const std::string& payload, Ctx&) {
    Frame f{.event_id = 0x04, .payload = payload + "-ok"};
    return encode(f);
}
} // namespace

TEST(RouterSink, DispatchReturnsEncodedFrame) {
    Router router;
    router.register_handler(0x03, echo_handler);
    InProcessRouterSink sink(router);
    Ctx ctx;

    PacketSink::Request req;
    req.conn_id = 1;
    req.event_id = 0x03;
    req.seq = 42;
    req.payload = "hi";

    auto reply = sink.handle(req, ctx);
    ASSERT_FALSE(reply.empty());

    // handler 编码时 seq=0, 由网关 stamp 回带请求 seq
    EXPECT_TRUE(stamp_seq(reply, req.seq));
    auto dec = decode(reply.data(), reply.size());
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(dec->frame.event_id, 0x04);
    EXPECT_EQ(dec->frame.seq, 42u);
    EXPECT_EQ(dec->frame.payload, "hi-ok");
}

TEST(RouterSink, UnknownEidReturnsEmpty) {
    Router router;
    InProcessRouterSink sink(router);
    Ctx ctx;

    PacketSink::Request req;
    req.conn_id = 1;
    req.event_id = 0x6666;   // 未注册
    req.seq = 7;
    req.payload = "x";

    EXPECT_TRUE(sink.handle(req, ctx).empty());
}

TEST(RouterSink, OneWayEventNoResponse) {
    // 模拟 0x15 位置上报: handler 返回空(单向事件)
    Router router;
    router.register_handler(0x15, [](const std::string&, Ctx&) {
        return std::vector<std::uint8_t>{};
    });
    InProcessRouterSink sink(router);
    Ctx ctx;

    PacketSink::Request req;
    req.conn_id = 9;
    req.event_id = 0x15;
    req.seq = 3;
    req.payload = "pos";

    EXPECT_TRUE(sink.handle(req, ctx).empty());
}

TEST(RouterSink, HandlerThrowIsCallerConcern) {
    // Sink 不吞异常(引擎侧 try/catch), 此处验证异常透传语义
    Router router;
    router.register_handler(0x01, [](const std::string&, Ctx&) -> std::vector<std::uint8_t> {
        throw std::runtime_error("boom");
    });
    InProcessRouterSink sink(router);
    Ctx ctx;

    PacketSink::Request req;
    req.event_id = 0x01;
    EXPECT_THROW(sink.handle(req, ctx), std::runtime_error);
}
