#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/router.hpp"

namespace bike::gateway {

// 网关唯一的业务出口。网关只做接入/帧编解码/解析派发, 业务逻辑不下沉。
// Step 2: InProcessRouterSink —— 进程内直连 Router, 可独立交付与回归。
// Step 3: RingDispatchSink —— 把请求帧写入 mmap SPSC 环, 由独立 Dispatch 进程消费;
//         响应经环回流后再走 OutboxKind::Respond。接口不变, 只换实现。
// 纯接口, 无系统调用, 跨平台可编译。
class PacketSink {
public:
    struct Request {
        std::uint64_t conn_id{0};
        std::uint16_t event_id{0};
        std::uint32_t seq{0};
        std::string payload;   // 原始 protobuf 字节
    };

    virtual ~PacketSink() = default;

    // 同步处理并返回完整响应帧(handler 内部已 encode)。
    // 响应 seq 由调用方(UringEngine)用 bike::stamp_seq 回带, 此处不处理。
    // 返回空 = 无响应(单向事件 0x15 / 未注册 eid)。在 worker 线程执行。
    virtual std::vector<std::uint8_t> handle(const Request& req,
                                             bike::server::Ctx& ctx) = 0;
};

// Step 2 stub: 直连 Router。注意 Router::dispatch 返回 handler 已编码的完整帧。
class InProcessRouterSink final : public PacketSink {
public:
    explicit InProcessRouterSink(bike::server::Router& router)
        : router_(router) {}

    std::vector<std::uint8_t> handle(const Request& req,
                                     bike::server::Ctx& ctx) override {
        return router_.dispatch(req.event_id, req.payload, ctx);
    }

private:
    bike::server::Router& router_;
};

} // namespace bike::gateway
