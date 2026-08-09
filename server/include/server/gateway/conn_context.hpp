#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "server/gateway/uring_op.hpp"

namespace bike::gateway {

// 连接状态机:
//   Active  — 有 recv 在途, 可接收新数据
//   Parsing — raw buffer 已移交 worker, 暂停 recv, 等 RearmRecv(eventfd)
//   Closing — 已发起关闭, 等在途 op 归零后释放
enum class ConnState : std::uint8_t { Active, Parsing, Closing };

// 每连接上下文。除标注外, 所有成员只允许主线程访问
// (worker 只持有 conn_id, 不持有 Connection 指针)。
// 纯数据结构, 无系统调用, 跨平台可编译。
class Connection {
public:
    Connection(int fd, std::uint64_t conn_id, std::size_t rx_buf_bytes)
        : fd_(fd), conn_id_(conn_id) {
        // 一次性把存储拉满到 size==capacity: 后续只用 rx_len_ 记账有效字节,
        // 绝不用 resize 增长(size 增长会 value-init 覆写内核 recv 已写入的数据)。
        pending_.resize(rx_buf_bytes);
    }

    // 兜底防 fd 泄漏: 正常路径 maybe_finalize 已 close 并置 -1(不会双关);
    // 仅当 reap 硬超时放弃连接等异常路径未关 fd 时才真正生效。
    ~Connection() {
#ifdef __linux__
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const noexcept { return fd_; }
    // 移交 fd 所有权(调用方接管 close 责任), 防析构双关
    int release_fd() noexcept { int f = fd_; fd_ = -1; return f; }
    std::uint64_t conn_id() const noexcept { return conn_id_; }

    // ---- rx 累积区(主线程独占) ----
    // 存储不变式: pending_.size() == capacity(全量预分配), rx_len_ 记录有效字节。
    // recv 写入 [data+rx_len_, size) 区间; commit 只动 rx_len_ 不碰存储,
    // 避免 resize 增长路径 value-init 覆写内核刚写入的字节(历史 bug)。

    // 确保尾部有 ≥ spare 字节空闲, 供 recv SQE 直接写入。
    // 必须在取 rx_writable() 之前调用(扩容可能移动存储)。
    void rx_prepare(std::size_t spare) {
        if (pending_.size() < rx_len_ + spare)
            pending_.resize(rx_len_ + spare);   // 只增长空闲区, 覆零无影响
    }
    std::span<std::uint8_t> rx_writable() {
        return {pending_.data() + rx_len_, pending_.size() - rx_len_};
    }
    // RECV cqe(res>0) 后登记写入字节数(内核已写入存储, 不得重建元素)
    void rx_commit(std::size_t n) { rx_len_ += n; }
    std::size_t rx_size() const noexcept { return rx_len_; }
    // 移交: swap 出存储后缩小 size 到有效长度(缩小不覆写保留字节),
    // 累积区重新拉满等待下轮。
    std::vector<std::uint8_t> rx_take(std::size_t reserve_cap) {
        std::vector<std::uint8_t> out;
        out.swap(pending_);
        out.resize(rx_len_);
        rx_len_ = 0;
        if (pending_.size() < reserve_cap)
            pending_.resize(reserve_cap);
        return out;
    }
    // RearmRecv 时回填 worker 交还的半包尾巴
    void rx_feed(std::vector<std::uint8_t> leftover) {
        if (rx_len_ + leftover.size() > pending_.size())
            pending_.resize(rx_len_ + leftover.size());
        std::copy(leftover.begin(), leftover.end(), pending_.begin() + rx_len_);
        rx_len_ += leftover.size();
    }

    // ---- send 队列(主线程独占) ----

    void send_push(std::vector<std::uint8_t> bytes) {
        send_bytes_ += bytes.size();
        send_q_.push_back(std::move(bytes));
    }
    bool send_pending() const noexcept { return !send_q_.empty(); }
    // std::deque 两端插入不失效既有元素的引用, 头块 span 在 send 在途期间稳定
    std::span<const std::uint8_t> send_front() const {
        return {send_q_.front().data(), send_q_.front().size()};
    }
    // 剥掉已发出的 n 字节; 头块未发完则原地裁剪续写(部分写)
    void send_pop(std::size_t n) {
        while (n > 0 && !send_q_.empty()) {
            auto& head = send_q_.front();
            if (n < head.size()) {
                head.erase(head.begin(), head.begin() + static_cast<std::ptrdiff_t>(n));
                send_bytes_ -= n;
                return;
            }
            n -= head.size();
            send_bytes_ -= head.size();
            send_q_.pop_front();
        }
    }
    std::size_t send_backlog_bytes() const noexcept { return send_bytes_; }
    bool send_active() const noexcept { return send_active_; }
    void set_send_active(bool v) noexcept { send_active_ = v; }

    ConnState state{ConnState::Active};
    int pending_ops{0};         // 在途 recv/send op 计数, Closing 归零后释放
    UringOp* recv_op{nullptr};  // 在途 recv op(供定向取消)
    UringOp* send_op{nullptr};  // 在途 send op(供定向取消)
    std::chrono::steady_clock::time_point last_active{std::chrono::steady_clock::now()};

private:
    int fd_{-1};
    std::uint64_t conn_id_{0};
    std::vector<std::uint8_t> pending_;   // 存储区(size==capacity 全量预分配)
    std::size_t rx_len_{0};               // pending_ 前 rx_len_ 字节为有效数据
    std::deque<std::vector<std::uint8_t>> send_q_;   // 发送队列
    std::size_t send_bytes_{0};
    bool send_active_{false};
};

} // namespace bike::gateway
