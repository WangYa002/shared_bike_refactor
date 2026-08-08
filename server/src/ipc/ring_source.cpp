// 模块三 RingSource 实现 (Linux-only, 设计稿 §6.3)。

#include "bike/ipc/ring_source.hpp"

#include <poll.h>

#include <cerrno>
#include <thread>

namespace bike::ipc {

namespace {

void cpu_pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
}

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

RingSource::RingSource(std::vector<ReqRing::Consumer> consumers,
                       FifoChannel* notify, int wake_fd, std::uint32_t spin_tries)
    : consumers_(std::move(consumers)), notify_(notify),
      wake_fd_(wake_fd), spin_tries_(spin_tries) {}

std::uint32_t RingSource::pop_once(
    const std::function<void(const ReqRing::Slot&)>& cb) {
    std::uint32_t total = 0;
    for (auto& cons : consumers_) {
        for (;;) {
            const ReqRing::Slot* batch = nullptr;
            const std::uint32_t n = cons.pop_batch(&batch, 64);
            if (n == 0) break;
            for (std::uint32_t i = 0; i < n; ++i) cb(batch[i]);
            cons.release(n);            // cb 完成才归还(槽内数据已拷走)
            total += n;
        }
    }
    return total;
}

std::uint32_t RingSource::wait_pop(
    const std::function<void(const ReqRing::Slot&)>& cb,
    std::chrono::milliseconds poll_slice) {
    // 1) 先无条件查环(通知只是提示, 可能合并/丢失)
    const std::uint32_t n = pop_once(cb);
    if (n > 0) return n;

    // 2) 自旋退避: 短时间忙等抓刚到达的请求, 避免 poll 系统调用开销
    for (std::uint32_t s = 0; s < spin_tries_; ++s) {
        cpu_pause();
        if (pop_once(cb) > 0) return 1;   // 条数不重要, 非零即"有产出"
    }

    // 3) poll 一个切片: req_notify FIFO + 停机 wake fd
    struct pollfd pfds[2];
    nfds_t npfd = 0;
    if (notify_ != nullptr && notify_->fd() >= 0) {
        pfds[npfd].fd = notify_->fd();
        pfds[npfd].events = POLLIN;
        pfds[npfd].revents = 0;
        ++npfd;
    }
    if (wake_fd_ >= 0) {
        pfds[npfd].fd = wake_fd_;
        pfds[npfd].events = POLLIN;
        pfds[npfd].revents = 0;
        ++npfd;
    }
    if (npfd > 0) {
        const int pr = ::poll(pfds, npfd,
                              static_cast<int>(poll_slice.count()));
        if (pr < 0 && errno != EINTR && errno != EAGAIN) return 0;
        // 唤醒后先读空 FIFO 再查环
        if (notify_ != nullptr) notify_->drain();
        // wake_fd 可读(停机): 直接返回 0, 由调用方检查停机标志
        for (nfds_t i = 0; i < npfd; ++i)
            if (pfds[i].fd == wake_fd_ && (pfds[i].revents & POLLIN))
                return 0;
    } else {
        std::this_thread::sleep_for(poll_slice);
    }

    // 4) 切片尾再查一次环(通知可能晚于查环到达)
    return pop_once(cb);
}

bool RingSource::producer_alive(std::chrono::milliseconds timeout) const {
    const std::uint64_t now = now_ns();
    const std::uint64_t limit =
        static_cast<std::uint64_t>(timeout.count()) * 1000000ull;
    for (const auto& cons : consumers_) {
        const std::uint64_t hb = cons.producer_heartbeat();
        if (hb != 0 && now - hb < limit) return true;
    }
    return false;
}

void RingSource::register_pid(std::uint32_t pid) {
    for (auto& c : consumers_) c.register_pid(pid);
}

void RingSource::heartbeat_all(std::uint64_t now) {
    for (auto& c : consumers_) c.heartbeat(now);
}

} // namespace bike::ipc
