// Linux-only: io_uring 网关引擎实现。
//
// 线程模型铁律(与 2026-08-08 设计稿第 4 节一致):
//   1. SQ(io_uring_get_sqe / io_uring_submit)只在本文件主循环线程操作;
//   2. 主线程对 RECV 完成零解析, raw buffer 整体移交 worker;
//   3. worker 完成后经 WorkerOutbox(eventfd)交还主线程补发 recv SQE / 排响应。

#include "server/gateway/uring_engine.hpp"

#include "server/gateway/frame_cut.hpp"
#include "server/logging.hpp"

#include <bike/protocol.hpp>

#include "bike/ipc/ipc_errors.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace bike::gateway {

namespace {
constexpr std::chrono::seconds kDrainDeadline{5};   // 优雅停机 drain 上限
constexpr std::chrono::seconds kReapHardLimit{2};   // 强制关闭后回收 CQE 上限
// SQ 耗尽触发的 drain 无外部停止信号兜底, 需独立硬上限防无限空转
constexpr std::chrono::seconds kExhaustDrainLimit{10};

std::uint64_t steady_now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
} // namespace

UringEngine::UringEngine(GatewayOptions opt,
                         bike::server::Router& router,
                         bike::server::Ctx& ctx,
                         std::unique_ptr<PacketSink> sink)
    : opt_(std::move(opt)),
      router_(router),
      ctx_(ctx),
      sink_(std::move(sink)),
      outbox_(),
      workers_(opt_.workers < 1 ? std::size_t{1}
                                : static_cast<std::size_t>(opt_.workers)) {
    stop_efd_ = ::eventfd(0, EFD_CLOEXEC);
    if (stop_efd_ < 0)
        throw std::runtime_error(std::string("stop eventfd() failed: ") +
                                 std::strerror(errno));
}

UringEngine::~UringEngine() {
    // 先拆 ring: 内核取消全部在途 op, 之后释放 Connection/op 才安全
    if (ring_ready_) io_uring_queue_exit(&ring_);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (stop_efd_ >= 0) ::close(stop_efd_);
}

void UringEngine::request_stop() noexcept {
    stopping_.store(true, std::memory_order_relaxed);
    // write() 是 async-signal-safe; 唤醒阻塞在 wait_cqe 的主循环
    if (stop_efd_ >= 0) {
        const std::uint64_t one = 1;
        (void)::write(stop_efd_, &one, sizeof(one));
    }
}

void UringEngine::attach_ipc(bike::ipc::RspRing::Consumer rsp, int rsp_notify_fd,
                             std::chrono::milliseconds peer_timeout) {
    rsp_consumer_ = std::move(rsp);
    rsp_notify_fd_ = rsp_notify_fd;
    ipc_peer_timeout_ = peer_timeout;
}

// ---------------------------------------------------------------- 主循环

void UringEngine::run() {
    open_listen_socket();
    init_ring();
    validate_sq_capacity();
    submit_accept();
    submit_eventfd_read();
    submit_stop_read();
    if (rsp_notify_fd_ >= 0) submit_rsp_notify_read();   // 模块三 ring 模式
    io_uring_submit(&ring_);
    BIKE_LOG_INFO("uring engine running: sq={} cq={} workers={} listen_fd={}",
                  opt_.sq_depth, opt_.cq_depth, opt_.workers, listen_fd_);

    for (;;) {
        struct io_uring_cqe* cqe = nullptr;
        struct __kernel_timespec ts{};
        ts.tv_sec = 1;   // 超时兜底: 驱动 sweep_idle / stopping_ 检查
        int rc = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
        if (rc == 0 && cqe != nullptr) {
            harvest();
        } else if (rc != 0 && rc != -ETIME && rc != -EINTR) {
            BIKE_LOG_ERROR("io_uring_wait_cqe_timeout failed: {}", std::strerror(-rc));
            break;
        }

        // 模块三: 每轮无条件查响应环(通知只是提示; 含对端判活节流)
        drain_response_ring();

        if (!draining_) sweep_idle();

        if (stopping_.load(std::memory_order_relaxed) && !draining_) {
            draining_ = true;
            accepting_ = false;
            if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
            // 正常信号停机取 drain deadline; SQ 耗尽已提前设过更长的兜底值
            if (drain_deadline_.time_since_epoch().count() == 0)
                drain_deadline_ = std::chrono::steady_clock::now() + kDrainDeadline;
            BIKE_LOG_INFO("graceful shutdown: draining {} connection(s)", conns_.size());
        }
        // harvest 之外的路径(sweep_idle/停机)也会挂 SQE(取消 op),
        // 每轮末尾统一提交; 无在途 SQE 时该调用不进内核。
        io_uring_submit(&ring_);
        if (draining_ && drain_complete()) break;
    }

    // 强制关闭残留连接, 并收割取消产生的 CQE(防止 Connection 悬空)。
    // 注意: close_conn 可能 erase conns_ 元素, 不可在遍历中直接调用。
    auto reap_limit = std::chrono::steady_clock::now() + kReapHardLimit;
    constexpr std::size_t kReapBatch = 64;   // 每轮最多关 64 连接, 防取消 SQE 突发爆 SQ
    while (!conns_.empty()) {
        std::vector<std::uint64_t> ids;
        ids.reserve(std::min<std::size_t>(conns_.size(), kReapBatch));
        for (auto& [id, c] : conns_) {
            if (ids.size() >= kReapBatch) break;
            ids.push_back(id);
        }
        for (auto id : ids) {
            auto it = conns_.find(id);
            if (it != conns_.end()) close_conn(*it->second, "shutdown");
        }
        if (conns_.empty()) break;
        io_uring_submit(&ring_);   // 把取消 SQE 提交下去, 才能收到 CQE
        if (std::chrono::steady_clock::now() > reap_limit) break;
        struct io_uring_cqe* cqe = nullptr;
        struct __kernel_timespec ts{};
        ts.tv_nsec = 100L * 1000L * 1000L;
        int rc = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
        if (rc == 0 && cqe != nullptr) harvest();
    }
    if (!conns_.empty()) {
        // 硬超时兜底: 不再等 CQE, 直接关 fd 并释放 Connection,
        // 避免 fd/UringOp 泄漏; io_uring_queue_exit 会取消残留在途 op。
        BIKE_LOG_WARN("reap deadline exceeded, force-closing {} connection(s)",
                      conns_.size());
        for (auto& [id, c] : conns_) {
            if (c->fd() >= 0) ::close(c->release_fd());
        }
        conns_.clear();
    }
    BIKE_LOG_INFO("gateway loop exited");
}

void UringEngine::open_listen_socket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error(std::string("socket() failed: ") + std::strerror(errno));

    int on = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(opt_.port));
    if (::inet_pton(AF_INET, opt_.listen.c_str(), &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error(std::string("bind() failed: ") + std::strerror(errno));
    if (::listen(listen_fd_, opt_.accept_backlog) < 0)
        throw std::runtime_error(std::string("listen() failed: ") + std::strerror(errno));
}

void UringEngine::init_ring() {
    struct io_uring_params params{};
    params.flags = IORING_SETUP_CQSIZE;
    params.cq_entries = static_cast<unsigned>(opt_.cq_depth);
    int rc = io_uring_queue_init_params(static_cast<unsigned>(opt_.sq_depth),
                                        &ring_, &params);
    if (rc < 0)
        throw std::runtime_error(std::string("io_uring_queue_init failed: ") +
                                 std::strerror(-rc));
    ring_ready_ = true;
}

void UringEngine::validate_sq_capacity() {
    // 启动校验: 停机时每连接最多两个定向取消 SQE(reap 已分批 ≤64 连接/轮,
    // 单轮突发 ≤128 个取消 SQE), 另需常驻 accept/wakeup/stop 三个 op。
    // 下限取 128 留足取消突发余量; 不足则启动报错,
    // 宁可配置失败, 不可运行中 SQ 耗尽被迫停机。
    constexpr int kMinSqDepth = 128;
    if (opt_.sq_depth < kMinSqDepth) {
        throw std::runtime_error(
            "[uring] sq_depth too small: need >= " + std::to_string(kMinSqDepth) +
            " (cancel-burst headroom), got " + std::to_string(opt_.sq_depth));
    }
}

void UringEngine::harvest() {
    unsigned head = 0;
    unsigned count = 0;
    struct io_uring_cqe* cqe = nullptr;
    io_uring_for_each_cqe(&ring_, head, cqe) {
        on_cqe(*cqe);
        ++count;
    }
    if (count > 0) {
        io_uring_cq_advance(&ring_, count);
        io_uring_submit(&ring_);   // 批量提交本轮收割中挂上的 SQE
    }
}

void UringEngine::on_cqe(struct io_uring_cqe& cqe) {
    auto* op = reinterpret_cast<UringOp*>(cqe.user_data);
    if (op == nullptr) return;   // 匿名取消 op 的 CQE
    switch (op->kind) {
    case OpKind::Accept: complete_accept(*op, cqe.res); break;
    case OpKind::Recv:   complete_recv(*op, cqe.res); break;
    case OpKind::Send:   complete_send(*op, cqe.res); break;
    case OpKind::Wakeup: complete_wakeup(*op); break;
    case OpKind::Stop:   complete_stop(*op); break;
    case OpKind::RspNotify: complete_rsp_notify(*op, cqe.res); break;
    }
}

// ---------------------------------------------------------------- CQE 处理

void UringEngine::complete_accept(UringOp& op, int res) {
    delete &op;
    if (!accepting_) {
        if (res >= 0) ::close(res);   // 停机期到达的新连接直接拒绝
        return;
    }
    if (res < 0) {
        if (res != -EINTR) {
            BIKE_LOG_WARN("accept failed: {}", std::strerror(-res));
            // EMFILE/ENFILE 类错误: 不再立即重挂形成 tight loop,
            // 连续失败超阈值后等主循环 1s 超时唤醒再重挂。
            if (++accept_fail_streak_ >= 8) {
                accept_fail_streak_ = 0;
                BIKE_LOG_WARN("accept failing repeatedly, backing off 1 tick");
                return;
            }
        }
        submit_accept();
        return;
    }
    accept_fail_streak_ = 0;
    int fd = res;
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    // fd 护栏: Connection 先接管所有权; 若首挂 recv 失败(SQ 耗尽等),
    // delete conn 由 ~Connection 兜底 close(fd), 绝不泄漏。
    Connection& conn = make_conn(fd);
    if (!submit_recv(conn)) {
        auto id = conn.conn_id();
        conns_.erase(id);   // ~Connection 关 fd
        BIKE_LOG_ERROR("conn {} closed: first recv submit failed", id);
    }
    submit_accept();
}

void UringEngine::complete_recv(UringOp& op, int res) {
    Connection& conn = *op.conn;
    conn.recv_op = nullptr;
    conn.pending_ops--;
    delete &op;

    if (conn.state == ConnState::Closing) { maybe_finalize(conn); return; }
    if (res < 0) { close_conn(conn, "recv error"); return; }
    if (res == 0)  { close_conn(conn, "peer closed"); return; }

    conn.rx_commit(static_cast<std::size_t>(res));
    conn.last_active = std::chrono::steady_clock::now();

    // 溢出保护: 半包 leftover(≤一个最大帧) + recv 空闲区 之外不应再增长
    if (conn.rx_size() >
        static_cast<std::size_t>(bike::kHeaderLen) + bike::kMaxMessageLen + opt_.rx_buf_bytes) {
        close_conn(conn, "rx overflow");
        return;
    }

    // 主线程零解析: 累积字节整体 swap 给 worker, 由其切帧 + protobuf 反序列化。
    // Parsing 期间不再为该连接提交 recv, 等 RearmRecv(eventfd) 再补发。
    auto raw = conn.rx_take(opt_.rx_buf_bytes);
    conn.state = ConnState::Parsing;
    workers_.post([this, conn_id = conn.conn_id(), raw = std::move(raw)]() mutable {
        worker_batch(conn_id, std::move(raw));
    });
}

void UringEngine::complete_send(UringOp& op, int res) {
    Connection& conn = *op.conn;
    conn.send_op = nullptr;
    conn.pending_ops--;
    conn.set_send_active(false);
    delete &op;

    if (conn.state == ConnState::Closing) { maybe_finalize(conn); return; }
    if (res < 0) { close_conn(conn, "send error"); return; }

    conn.send_pop(static_cast<std::size_t>(res));   // 部分写: 头块裁剪后续写
    conn.last_active = std::chrono::steady_clock::now();
    // SQ 耗尽时 submit_send 返回 false: 数据留在队列, 下次再武装时重发
    if (conn.send_pending()) submit_send(conn);
}

void UringEngine::complete_wakeup(UringOp& op) {
    delete &op;
    submit_eventfd_read();   // 一次性 read, 重新武装
    drain_outbox();
}

void UringEngine::complete_stop(UringOp& op) {
    delete &op;
    stopping_.store(true, std::memory_order_relaxed);
    // 不重挂 stop read: 停一次就够
}

void UringEngine::complete_rsp_notify(UringOp& op, int res) {
    delete &op;
    if (res == -ECANCELED) return;   // 停机取消: 不重挂
    if (res < 0) {
        BIKE_LOG_WARN("rsp notify read failed: {}", std::strerror(-res));
        // 持续失败退避(参照 accept_fail_streak_): 不立即重挂形成 tight loop,
        // 超阈值后退一拍(由后续 drain/CQE 时机重新武装)。
        if (++rsp_notify_fail_streak_ >= 8) {
            rsp_notify_fail_streak_ = 0;
            BIKE_LOG_WARN("rsp notify read failing repeatedly, backing off 1 tick");
        } else {
            submit_rsp_notify_read();
        }
        return;
    }
    rsp_notify_fail_streak_ = 0;
    submit_rsp_notify_read();        // 一次性 read, 重新武装
    drain_response_ring();           // 通知到 → 立即抽环(不等下轮)
}

// ---------------------------------------------------------------- outbox 派发

bool UringEngine::push_response(Connection& conn, std::vector<std::uint8_t> frame) {
    if (conn.send_backlog_bytes() + frame.size() > opt_.send_backlog_limit) {
        close_conn(conn, "send backlog overflow");
        return false;
    }
    conn.send_push(std::move(frame));
    if (!conn.send_active()) submit_send(conn);
    return true;
}

// 模块三: 响应环 → 客户端。槽内即 Dispatch 已 stamp_seq 的完整 FBEB 帧,
// 主线程零解析直发; 连接不存在/已关则丢弃(请求期间断连是常态)。
void UringEngine::drain_response_ring() {
    if (!rsp_consumer_.valid()) return;

    // ---- 1s 节流: 自心跳 + Dispatch 判活(进程级恢复决策, 设计稿 §7.6) ----
    const auto now_tp = std::chrono::steady_clock::now();
    if (now_tp - last_rsp_hb_ >= std::chrono::seconds(1)) {
        last_rsp_hb_ = now_tp;
        rsp_consumer_.heartbeat(steady_now_ns());
        if (!stopping_.load(std::memory_order_relaxed)) {
            const std::uint32_t pid = rsp_consumer_.producer_pid();
            const std::uint64_t hb = rsp_consumer_.producer_heartbeat();
            const std::uint64_t now = steady_now_ns();
            const std::uint64_t limit =
                static_cast<std::uint64_t>(ipc_peer_timeout_.count()) * 1000000ull;
            bool dead = false;
            if (pid != 0 && ::kill(static_cast<pid_t>(pid), 0) < 0 && errno != EPERM)
                dead = true;
            else if (hb != 0 && now - hb > limit)
                dead = true;
            if (dead) {
                BIKE_LOG_ERROR("dispatch peer dead (pid={} hb_lag_ms={}); "
                               "requesting restart (compose will relaunch)",
                               pid, hb == 0 ? -1
                                    : static_cast<long>((now - hb) / 1000000));
                request_stop();
            }
        }
    }

    // ---- 30s 节流: 周期指标汇总(设计稿 §9.4, 响应环 pop 峰值) ----
    if (now_tp - last_rsp_metrics_ >= std::chrono::seconds(30)) {
        last_rsp_metrics_ = now_tp;
        BIKE_LOG_INFO("gateway rsp ring metrics: popped_total={} batch_peak={}",
                      rsp_popped_total_, rsp_batch_peak_);
    }

    // ---- 抽环直发 ----
    for (;;) {
        const bike::ipc::RspRing::Slot* batch = nullptr;
        const std::uint32_t n = rsp_consumer_.pop_batch(&batch, 32);
        if (n == 0) break;
        rsp_popped_total_ += n;
        if (n > rsp_batch_peak_) rsp_batch_peak_ = n;
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto& pkt = batch[i].pkt;
            if (pkt.payload_len == 0) continue;
            // 上界校验(与 ring_source.cpp 对称): 防坏槽越界读并直发客户端
            if (pkt.payload_len > bike::ipc::RspRing::kPayloadMax) {
                BIKE_LOG_ERROR("rsp slot payload_len {} exceeds max {}, skipping",
                               pkt.payload_len, bike::ipc::RspRing::kPayloadMax);
                continue;
            }
            auto it = conns_.find(pkt.conn_id);
            if (it == conns_.end()) continue;
            Connection& conn = *it->second;
            if (conn.state == ConnState::Closing) continue;
            push_response(conn, std::vector<std::uint8_t>(
                                    batch[i].payload,
                                    batch[i].payload + pkt.payload_len));
        }
        rsp_consumer_.release(n);   // 发送拷贝完成后归还槽
    }
}

void UringEngine::drain_outbox() {
    std::vector<OutboxItem> items;
    outbox_.drain(items);
    for (auto& it : items) {
        auto found = conns_.find(it.conn_id);
        if (found == conns_.end()) continue;   // 连接已关闭, 丢弃
        Connection& conn = *found->second;
        if (conn.state == ConnState::Closing) continue;

        switch (it.kind) {
        case OutboxKind::Respond:
            push_response(conn, std::move(it.bytes));
            break;
        case OutboxKind::RearmRecv:
            if (draining_) {   // 停机期: 完成在途处理后直接关, 不再收新请求
                close_conn(conn, "shutdown");
                break;
            }
            if (conn.state != ConnState::Parsing) break;   // 防御: 状态不符
            conn.rx_feed(std::move(it.bytes));   // 回填半包尾巴
            conn.state = ConnState::Active;
            // 下一个 Recv SQE 由主线程提交; SQ 耗尽失败时关连接兜底,
            // 避免连接永久滞留 Parsing 态。
            if (!submit_recv(conn)) close_conn(conn, "rearm recv submit failed");
            break;
        case OutboxKind::Close:
            close_conn(conn, "bad frame (worker)");
            break;
        }
    }
}

// ---------------------------------------------------------------- 连接管理

Connection& UringEngine::make_conn(int fd) {
    std::uint64_t id = next_conn_id_++;
    auto [it, inserted] = conns_.emplace(
        id, std::make_unique<Connection>(fd, id, opt_.rx_buf_bytes));
    (void)inserted;
    return *it->second;
}

void UringEngine::close_conn(Connection& conn, const char* reason) {
    if (conn.state == ConnState::Closing) return;
    conn.state = ConnState::Closing;
    BIKE_LOG_DEBUG("conn {} closing: {}", conn.conn_id(), reason);
    // 定向取消在途 op; 被取消 op 的 CQE(-ECANCELED)与取消 op 自身 CQE
    // 都会到达, pending_ops 归零后才真正 close(fd)+释放。
    // 取消 SQE 提交失败(SQ 耗尽)时由 reap 硬超时兜底强关。
    if (conn.recv_op != nullptr) submit_cancel(conn.recv_op);
    if (conn.send_op != nullptr) submit_cancel(conn.send_op);
    maybe_finalize(conn);
}

void UringEngine::maybe_finalize(Connection& conn) {
    if (conn.state != ConnState::Closing || conn.pending_ops > 0) return;
    ::close(conn.release_fd());   // 移交 fd 所有权后关, 防 ~Connection 双关
    conns_.erase(conn.conn_id());
}

void UringEngine::sweep_idle() {
    if (opt_.idle_timeout.count() <= 0) return;
    auto now = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> victims;
    for (auto& [id, c] : conns_) {
        if (c->state != ConnState::Closing && now - c->last_active > opt_.idle_timeout)
            victims.push_back(id);
    }
    for (auto id : victims) {
        auto it = conns_.find(id);
        if (it != conns_.end()) close_conn(*it->second, "idle timeout");
    }
}

bool UringEngine::drain_complete() {
    if (conns_.empty()) return true;
    if (std::chrono::steady_clock::now() > drain_deadline_) {
        BIKE_LOG_WARN("drain deadline exceeded with {} connection(s)", conns_.size());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- SQ 提交(仅主线程)

struct io_uring_sqe* UringEngine::get_sqe() {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
        io_uring_submit(&ring_);   // SQ 满: 先提交腾位
        sqe = io_uring_get_sqe(&ring_);
    }
    if (sqe == nullptr) {
        // 可恢复降级: 不抛异常(抛会沿 CQE 回调链直达 main 触发 terminate)。
        // 置 draining 由主循环进入排空停机; 漏挂的 recv 由 idle sweep/
        // drain 路径兜底关连接。
        if (!sq_fail_logged_) {
            sq_fail_logged_ = true;
            BIKE_LOG_ERROR("SQ exhausted — entering drain; increase [uring] sq_depth");
            drain_deadline_ = std::chrono::steady_clock::now() + kExhaustDrainLimit;
        }
        draining_ = true;
        return nullptr;
    }
    return sqe;
}

void UringEngine::submit_accept() {
    auto* sqe = get_sqe();
    if (sqe == nullptr) {
        BIKE_LOG_WARN("submit_accept skipped: SQ exhausted, re-arm next tick");
        return;   // 停机/耗尽: 下轮超时唤醒再重挂
    }
    auto* op = new UringOp{};
    op->kind = OpKind::Accept;
    // 5 参签名(liburing >=2.0 通用): 4 参便捷形式是 2.3+ 才有, Ubuntu 22.04 编不过
    io_uring_prep_accept(sqe, listen_fd_, nullptr, nullptr, 0);
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
}

bool UringEngine::submit_recv(Connection& conn) {
    conn.rx_prepare(opt_.rx_buf_bytes);          // 先扩容, 再取 span(存储稳定)
    auto span = conn.rx_writable();

    // 先取 SQE 再记账: 失败时不留下永久挂账与 op 泄漏
    auto* sqe = get_sqe();
    if (sqe == nullptr) return false;

    auto* op = new UringOp{};
    op->kind = OpKind::Recv;
    op->conn = &conn;
    conn.recv_op = op;
    conn.pending_ops++;

    io_uring_prep_recv(sqe, conn.fd(), span.data(), span.size(), 0);
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
    return true;
}

bool UringEngine::submit_send(Connection& conn) {
    auto span = conn.send_front();   // 头块在 send 在途期间不被修改, 稳定

    // 先取 SQE 再记账: 失败时数据留在队列, 下次再武装时重发
    auto* sqe = get_sqe();
    if (sqe == nullptr) return false;

    auto* op = new UringOp{};
    op->kind = OpKind::Send;
    op->conn = &conn;
    conn.send_op = op;
    conn.pending_ops++;
    conn.set_send_active(true);

    io_uring_prep_send(sqe, conn.fd(), span.data(), span.size(), MSG_NOSIGNAL);
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
    return true;
}

void UringEngine::submit_eventfd_read() {
    auto* sqe = get_sqe();
    if (sqe == nullptr) return;
    auto* op = new UringOp{};
    op->kind = OpKind::Wakeup;
    io_uring_prep_read(sqe, outbox_.fd(), &op->event_val,
                       sizeof(op->event_val), 0);
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
}

void UringEngine::submit_stop_read() {
    auto* sqe = get_sqe();
    if (sqe == nullptr) return;
    auto* op = new UringOp{};
    op->kind = OpKind::Stop;
    io_uring_prep_read(sqe, stop_efd_, &op->event_val,
                       sizeof(op->event_val), 0);
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
}

void UringEngine::submit_rsp_notify_read() {
    auto* sqe = get_sqe();
    if (sqe == nullptr) return;   // 下轮超时唤醒会重新 drain; 重挂由 RspNotify CQE 驱动
    auto* op = new UringOp{};
    op->kind = OpKind::RspNotify;
    // FIFO/管道类 fd: offset 传 -1(非可寻址设备)
    io_uring_prep_read(sqe, rsp_notify_fd_, rsp_notify_buf_,
                       sizeof(rsp_notify_buf_), static_cast<__u64>(-1));
    sqe->user_data = reinterpret_cast<std::uint64_t>(op);
}

bool UringEngine::submit_cancel(UringOp* target) {
    auto* sqe = get_sqe();
    if (sqe == nullptr) return false;
    io_uring_prep_cancel(sqe, target, 0);
    sqe->user_data = 0;   // 匿名: on_cqe 中直接忽略其 CQE
    return true;
}

// ---------------------------------------------------------------- worker 侧

void UringEngine::worker_batch(std::uint64_t conn_id, std::vector<std::uint8_t> raw) {
    std::vector<bike::Frame> frames;
    CutResult cut = cut_frames(raw.data(), raw.size(), frames);

    for (auto& f : frames) {
        PacketSink::Request req;
        req.conn_id = conn_id;
        req.event_id = f.event_id;
        req.seq = f.seq;
        req.payload = std::move(f.payload);

        std::vector<std::uint8_t> reply;
        try {
            // inprocess: 进程内直连 Router; ring: 写请求环(响应异步回流)
            reply = sink_->handle(req, ctx_);
        } catch (const bike::ipc::SinkOverload&) {
            // 模块三: 请求环满 = 对端背压, 关连泄压(设计稿 §6.2)
            BIKE_LOG_ERROR("ipc request ring full, closing conn {}", conn_id);
            outbox_.push(OutboxItem{OutboxKind::Close, conn_id, {}});
            return;
        } catch (const bike::ipc::MalformedIpcRequest& e) {
            BIKE_LOG_ERROR("ipc malformed request conn={}: {}", conn_id, e.what());
            outbox_.push(OutboxItem{OutboxKind::Close, conn_id, {}});
            return;
        } catch (const std::exception& e) {
            BIKE_LOG_ERROR("dispatch eid={} conn={} threw: {}",
                           req.event_id, conn_id, e.what());
        } catch (...) {
            // 非 std 异常也兜住: 单帧失败不得杀死 worker 线程/整批处理
            BIKE_LOG_ERROR("dispatch eid={} conn={} threw non-std exception",
                           req.event_id, conn_id);
        }
        if (!reply.empty()) {
            // ring 模式下 sink 返回空帧, 不会走到这里(Dispatch 侧 stamp)。
            // 回带请求 seq(handler 编码时 seq=0); 失败说明帧已损坏,
            // 记错误日志并丢弃该响应帧, 绝不发脏帧给客户端。
            if (bike::stamp_seq(reply, req.seq)) {
                outbox_.push(OutboxItem{OutboxKind::Respond, conn_id, std::move(reply)});
            } else {
                BIKE_LOG_ERROR("stamp_seq failed: dropping reply eid={} conn={}",
                               req.event_id, conn_id);
            }
        }
    }

    if (cut.bad) {
        outbox_.push(OutboxItem{OutboxKind::Close, conn_id, {}});
        return;   // 不回 RearmRecv, 主线程将关闭连接
    }
    outbox_.push(OutboxItem{OutboxKind::RearmRecv, conn_id,
                            std::vector<std::uint8_t>(cut.leftover,
                                                      cut.leftover + cut.leftover_len)});
}

} // namespace bike::gateway
