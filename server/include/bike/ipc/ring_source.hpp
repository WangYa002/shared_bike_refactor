#pragma once

// 模块三 RingSource: Dispatch 侧请求环读取 (Linux-only, 设计稿 §6.3)。
// 单 RingReader 线程轮询 N 个请求环(consumer 视图, 每环单读者),
// 空环时按 "自旋退避 → poll(req_notify FIFO + 停机 wake fd)" 休眠。

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

#include "bike/ipc/fifo_channel.hpp"
#include "bike/ipc/spsc_ring.hpp"

namespace bike::ipc {

class RingSource {
public:
    // consumers: 每请求环一个 consumer(attach_req_consumers 结果);
    // notify: req_notify FIFO(读端); wake_fd: 停机唤醒 fd(readable 即醒)。
    RingSource(std::vector<ReqRing::Consumer> consumers, FifoChannel* notify,
               int wake_fd, std::uint32_t spin_tries);

    // 单轮: 查环 → 自旋退避 → poll 一个切片 → 再查环; 返回本条数(可为 0)。
    // 返回 0 且 wake_fd 可读 = 停机信号, 由调用方检查停机标志。
    // 调用方在外层循环交替调用, 交错处理其它职责(如冲刷响应环)。
    // cb 对每条在槽归还前调用; cb 抛异常将直接传播(调用方保证不抛)。
    std::uint32_t wait_pop(const std::function<void(const ReqRing::Slot&)>& cb,
                           std::chrono::milliseconds poll_slice =
                               std::chrono::milliseconds(200));

    // 任一请求环的 Gateway producer 心跳新于 timeout 即视为对端存活(设计稿 §5.7)
    bool producer_alive(std::chrono::milliseconds timeout) const;

    // consumer 侧注册与心跳(供 Gateway 反向判活用)
    void register_pid(std::uint32_t pid);
    void heartbeat_all(std::uint64_t now_ns);

private:
    std::uint32_t pop_once(const std::function<void(const ReqRing::Slot&)>& cb);

    std::vector<ReqRing::Consumer> consumers_;
    FifoChannel* notify_;                 // 不拥有, 可为 nullptr(纯轮询)
    int wake_fd_{-1};
    std::uint32_t spin_tries_{64};
};

} // namespace bike::ipc
