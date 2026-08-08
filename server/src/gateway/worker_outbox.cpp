// Linux-only: WorkerOutbox 的 eventfd 粘合实现。

#include "server/gateway/worker_outbox.hpp"
#include "server/logging.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <stdexcept>

namespace bike::gateway {

WorkerOutbox::WorkerOutbox() {
    efd_ = ::eventfd(0, EFD_CLOEXEC);
    if (efd_ < 0) throw std::runtime_error("eventfd() failed");
}

WorkerOutbox::~WorkerOutbox() {
    if (efd_ >= 0) ::close(efd_);
}

void WorkerOutbox::push(OutboxItem item) {
    q_.push(std::move(item));
    // eventfd 计数语义: 多次写累积, 主线程一次 read 即清空并收到 cqe
    const std::uint64_t one = 1;
    if (::write(efd_, &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one)))
        BIKE_LOG_WARN("worker outbox eventfd write failed");
}

} // namespace bike::gateway
