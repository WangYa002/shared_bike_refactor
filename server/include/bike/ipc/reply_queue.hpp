#pragma once

// 模块三 Dispatch 进程内响应汇聚队列 (设计稿 §3.3/§7.6)。
// 业务线程(M 个) → RingReader(响应环唯一 producer) 的 MPSC 通道。
// 纯逻辑(mutex + deque), 无系统调用, 跨平台可单测;
// eventfd 唤醒粘合在 dispatch_main 侧(Linux-only)。
// 模式与 gateway OutboxQueue(outbox_queue.hpp)一致。

#include <cstdint>
#include <deque>
#include <iterator>
#include <mutex>
#include <vector>

namespace bike::ipc {

// 一个已完成业务处理、待写入响应环的响应帧。
struct ReplyItem {
    std::uint64_t conn_id{0};        // 回投目标连接
    std::uint32_t seq{0};            // 请求 seq(已 stamp 进 frame 则不再使用)
    bool one_way{false};             // 单向事件标记
    std::vector<std::uint8_t> frame; // handler 已编码的完整帧(seq=0, 待 stamp)
};

// MPSC: push 由业务线程调用, drain 仅 RingReader 线程调用。
class ReplyQueue {
public:
    void push(ReplyItem item) {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(item));
    }

    // 消费者一次性取空
    std::size_t drain(std::vector<ReplyItem>& out) {
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
    std::deque<ReplyItem> q_;
};

} // namespace bike::ipc
