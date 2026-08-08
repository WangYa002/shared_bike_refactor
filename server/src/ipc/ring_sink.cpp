// 模块三 RingSink 实现 (Linux-only, 设计稿 §6.2)。

#include "bike/ipc/ring_sink.hpp"

#include <bike/protocol.hpp>

#include <stdexcept>

#include "bike/ipc/ipc_errors.hpp"
#include "bike/ipc/ipc_packet.hpp"
#include "server/logging.hpp"

namespace bike::ipc {

RingSink::RingSink(std::vector<ReqRing::Producer> producers, FifoChannel* notify)
    : producers_(std::move(producers)), notify_(notify) {
    if (producers_.empty())
        throw std::runtime_error("ipc ring sink: empty producer pool");
}

ReqRing::Producer& RingSink::bind_producer() {
    // SPSC 关键约束: 一个 producer 视图终生绑定一个 worker 线程。
    static thread_local int t_idx = -1;
    if (t_idx < 0) {
        const std::uint32_t idx =
            next_idx_.fetch_add(1, std::memory_order_relaxed);
        if (idx >= producers_.size())
            throw std::runtime_error(
                "ipc ring sink: worker count exceeds request ring pool");
        t_idx = static_cast<int>(idx);
    }
    return producers_[static_cast<std::size_t>(t_idx)];
}

std::vector<std::uint8_t> RingSink::handle(const Request& req,
                                           bike::server::Ctx& /*ctx*/) {
    ReqRing::Producer& prod = bind_producer();

    if (req.payload.size() > ReqRing::kPayloadMax)
        throw MalformedIpcRequest();

    IpcPacket pkt;
    pkt.conn_id = req.conn_id;
    pkt.event_id = req.event_id;
    pkt.seq = req.seq;
    pkt.payload_len = static_cast<std::uint32_t>(req.payload.size());
    if (req.event_id == bike::event_id(bike::Event::RidePositionReport))
        pkt.flags |= kFlagOneWay;

    const bool was_empty = prod.empty();

    ReqRing::Slot* slot = prod.begin_write();
    if (slot == nullptr) {
        overload_count_.fetch_add(1, std::memory_order_relaxed);
        throw SinkOverload();          // 环满 → 引擎关连该连接(背压)
    }
    ReqRing::Producer::fill(slot, pkt, req.payload.data());
    prod.publish(1);

    // in-flight 峰值水位采样(设计稿 §9.4): 单环维度 CAS 抬升
    const std::uint32_t cur = prod.size();
    std::uint32_t peak = inflight_peak_.load(std::memory_order_relaxed);
    while (cur > peak &&
           !inflight_peak_.compare_exchange_weak(peak, cur,
                                                 std::memory_order_relaxed)) {}

    // 仅环由空转非空时通知, 压缩 FIFO 写次数
    if (was_empty && notify_ != nullptr)
        notify_->write1();

    return {};                          // 响应经响应环异步回流
}

void RingSink::register_pids(std::uint32_t pid) {
    for (auto& p : producers_) p.register_pid(pid);
}

void RingSink::heartbeat_all(std::uint64_t now_ns) {
    // 只写共享头的 producer_heartbeat_ns, 不触碰 head_local_, 跨线程安全
    for (auto& p : producers_) p.heartbeat(now_ns);

    // ---- 30s 节流: 周期指标汇总(INFO, 随 [log].level 生效) ----
    if (now_ns - last_metrics_ns_ >= 30ull * 1000000000ull) {
        last_metrics_ns_ = now_ns;
        std::uint32_t inflight_now = 0;
        for (auto& p : producers_) inflight_now += p.size();
        BIKE_LOG_INFO("ring sink metrics: overload_total={} inflight_peak={} "
                      "inflight_now={} rings={}",
                      overload_count_.load(std::memory_order_relaxed),
                      inflight_peak_.load(std::memory_order_relaxed),
                      inflight_now, producers_.size());
    }
}

} // namespace bike::ipc
