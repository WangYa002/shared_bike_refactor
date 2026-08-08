#pragma once

#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <vector>

namespace bike::gateway {

// worker 交还主线程的三种意图(worker→主线程唯一通道载荷)
enum class OutboxKind : std::uint8_t {
    Respond,    // bytes = 已编码(已 stamp seq)响应帧, 入 send 队列
    RearmRecv,  // bytes = 未切完的半包尾巴, 回填 rx 后补发 recv SQE
    Close,      // 坏帧/解析失败, 关闭连接
};

struct OutboxItem {
    OutboxKind kind{};
    std::uint64_t conn_id{0};
    std::vector<std::uint8_t> bytes;
};

// 纯逻辑 MPSC 队列(mutex + deque): 无系统调用, 跨平台可单测。
// eventfd 粘合见 Linux-only 的 WorkerOutbox(worker_outbox.hpp)。
class OutboxQueue {
public:
    void push(OutboxItem item) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(item));
    }
    // 消费者(主线程)一次性取空
    std::size_t drain(std::vector<OutboxItem>& out) {
        std::lock_guard<std::mutex> lk(mu_);
        std::size_t n = q_.size();
        if (n > 0) {
            out.insert(out.end(),
                       std::make_move_iterator(q_.begin()),
                       std::make_move_iterator(q_.end()));
            q_.clear();
        }
        return n;
    }
    std::size_t size() {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

private:
    std::mutex mu_;
    std::deque<OutboxItem> q_;
};

} // namespace bike::gateway
