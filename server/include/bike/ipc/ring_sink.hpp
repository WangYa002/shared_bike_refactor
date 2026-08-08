#pragma once

// 模块三 RingSink: Gateway worker 线程 → mmap 请求环 (Linux-only, 设计稿 §6.2)。
// 实现 PacketSink 接口: handle() 将请求写入 per-worker 请求环, 响应异步经响应环回流。
// producer 视图按 thread_local 绑定(严格 SPSC: 每环单写者 = 单 worker 线程)。
// 环满抛 SinkOverload、payload 超限抛 MalformedIpcRequest(引擎据此关连)。

#include <atomic>
#include <cstdint>
#include <vector>

#include "bike/ipc/fifo_channel.hpp"
#include "bike/ipc/spsc_ring.hpp"
#include "server/gateway/packet_sink.hpp"

namespace bike::ipc {

class RingSink final : public bike::gateway::PacketSink {
public:
    // producers: 每 worker 一个请求环 producer(attach_req_producers 结果);
    // notify: req_notify FIFO(Gateway 写, Dispatch 读), 可为 nullptr(轮询模式)。
    RingSink(std::vector<ReqRing::Producer> producers, FifoChannel* notify);

    // PacketSink: 写环成功即返回空帧(响应异步)。
    // 抛 bike::ipc::SinkOverload(环满) / MalformedIpcRequest(payload 超限)。
    std::vector<std::uint8_t> handle(const Request& req,
                                     bike::server::Ctx& ctx) override;

    // 心跳线程: 每 1s 更新全部请求环 producer 心跳 + pid 注册;
    // 内部附 30s 节流的周期指标汇总日志(设计稿 §9.4)。
    void register_pids(std::uint32_t pid);
    void heartbeat_all(std::uint64_t now_ns);

    std::size_t ring_count() const { return producers_.size(); }

    // ---- 可观测(设计稿 §9.4): 供测试/运维读数 ----
    std::uint64_t overload_count() const {
        return overload_count_.load(std::memory_order_relaxed);
    }
    std::uint32_t inflight_peak() const {
        return inflight_peak_.load(std::memory_order_relaxed);
    }

private:
    ReqRing::Producer& bind_producer();   // thread_local 首次绑定, 池耗尽抛异常

    std::vector<ReqRing::Producer> producers_;
    FifoChannel* notify_;                 // 不拥有
    std::atomic<std::uint32_t> next_idx_{0};
    std::atomic<std::uint64_t> overload_count_{0};   // SinkOverload 累计次数
    std::atomic<std::uint32_t> inflight_peak_{0};    // 请求环 in-flight 峰值水位(单环)
    std::uint64_t last_metrics_ns_{0};               // 指标日志 30s 节流(心跳线程独享)
};

} // namespace bike::ipc
