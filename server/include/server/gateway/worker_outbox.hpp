#pragma once

// Linux-only: eventfd 粘合层。Windows 侧请勿包含本头文件,
// 纯逻辑队列见 outbox_queue.hpp (OutboxQueue)。

#include <cstddef>
#include <vector>

#include "server/gateway/outbox_queue.hpp"

namespace bike::gateway {

// worker→主线程交付通道: OutboxQueue + eventfd。
// push(worker 线程): 入队 + eventfd 写 1 唤醒主线程;
// drain(主线程): eventfd cqe 到达后一次性取空队列。
class WorkerOutbox {
public:
    WorkerOutbox();   // eventfd(0, EFD_CLOEXEC), 失败抛 std::runtime_error
    ~WorkerOutbox();

    WorkerOutbox(const WorkerOutbox&) = delete;
    WorkerOutbox& operator=(const WorkerOutbox&) = delete;

    int fd() const noexcept { return efd_; }

    void push(OutboxItem item);   // worker 线程调用
    // 仅主线程调用
    std::size_t drain(std::vector<OutboxItem>& out) { return q_.drain(out); }

private:
    OutboxQueue q_;
    int efd_{-1};
};

} // namespace bike::gateway
