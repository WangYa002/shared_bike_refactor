#pragma once

// Linux-only: io_uring 网关引擎。Windows 侧请勿包含本头文件。

#include <liburing.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/router.hpp"
#include "server/util/thread_pool.hpp"
#include "server/gateway/conn_context.hpp"
#include "server/gateway/packet_sink.hpp"
#include "server/gateway/uring_op.hpp"
#include "server/gateway/worker_outbox.hpp"

namespace bike::gateway {

struct GatewayOptions {
    std::string listen{"0.0.0.0"};
    int port{8888};
    int sq_depth{256};                        // [uring] sq_depth
    int cq_depth{512};                        // [uring] cq_depth
    int workers{8};                           // [uring] workers
    std::size_t rx_buf_bytes{65536};          // 单连接单次 recv 空闲区目标
    std::size_t send_backlog_limit{1 << 20};  // 单连接发送背压上限(默认 1MB)
    std::chrono::milliseconds idle_timeout{60000};
    int accept_backlog{1024};
};

// 单主线程 io_uring 引擎。约束:
//   - io_uring_get_sqe / io_uring_submit 只发生在 run() 所在线程(严禁多线程碰 SQ);
//   - worker 线程只通过 WorkerOutbox(eventfd) 与主线程通信;
//   - Connection 及其 rx/send 缓冲只由主线程读写(worker 只持有 conn_id);
//   - 主线程对 RECV 完成零解析: 累积字节整体移交 worker 切帧+反序列化。
class UringEngine {
public:
    UringEngine(GatewayOptions opt,
                bike::server::Router& router,
                bike::server::Ctx& ctx,
                std::unique_ptr<PacketSink> sink);  // Step2 传 InProcessRouterSink
    ~UringEngine();

    UringEngine(const UringEngine&) = delete;
    UringEngine& operator=(const UringEngine&) = delete;

    // 阻塞当前线程运行事件循环, 直到收到停止请求且 drain 完成。
    void run();

    // 信号处理中调用(async-signal-safe: 原子置位 + write(stop_eventfd))。
    void request_stop() noexcept;

private:
    // ---- 主循环 ----
    void open_listen_socket();
    void init_ring();
    void validate_sq_capacity();       // 启动时校验 sq_depth 足够, 不足报错
    void harvest();                    // io_uring_for_each_cqe 批量收割 + 批量提交
    void on_cqe(struct io_uring_cqe& cqe);
    void complete_accept(UringOp& op, int res);
    void complete_recv(UringOp& op, int res);
    void complete_send(UringOp& op, int res);
    void complete_wakeup(UringOp& op);
    void complete_stop(UringOp& op);
    void drain_outbox();               // 处理 worker 交还项(Respond/RearmRecv/Close)
    void sweep_idle();                 // 空闲超时清理(每轮收割后)
    bool drain_complete();             // 停机期: 连接排空或 deadline 到

    // ---- SQ 提交(仅主线程) ----
    // SQ 满时先 submit 重试; 仍失败返回 nullptr 并置 draining_ 触发排空停机,
    // 调用点判空跳过本次提交(绝不抛异常, 避免沿 CQE 回调链 terminate)。
    struct io_uring_sqe* get_sqe();
    void submit_accept();              // SQ 耗尽时静默跳过, 下轮超时唤醒重挂
    bool submit_recv(Connection& conn);   // 失败 false(调用方负责处置连接)
    bool submit_send(Connection& conn);   // 失败 false(数据留队列等再武装)
    void submit_eventfd_read();        // 重新武装 Wakeup op
    void submit_stop_read();           // stop_eventfd 读(信号唤醒)
    bool submit_cancel(UringOp* target); // 定向取消在途 op(匿名 SQE); 失败 false

    // ---- 连接管理 ----
    Connection& make_conn(int fd);
    void close_conn(Connection& conn, const char* reason); // 置 Closing + 取消在途 op
    void maybe_finalize(Connection& conn);                 // pending_ops==0 时 close(fd)+erase

    // ---- worker 侧 ----
    void worker_batch(std::uint64_t conn_id, std::vector<std::uint8_t> raw);

    GatewayOptions opt_;
    bike::server::Router& router_;
    bike::server::Ctx& ctx_;
    std::unique_ptr<PacketSink> sink_;

    struct io_uring ring_{};
    bool ring_ready_{false};
    int listen_fd_{-1};
    int stop_efd_{-1};
    std::atomic<bool> stopping_{false};
    bool draining_{false};
    std::chrono::steady_clock::time_point drain_deadline_{};

    std::uint64_t next_conn_id_{1};
    std::unordered_map<std::uint64_t, std::unique_ptr<Connection>> conns_;
    bool accepting_{true};
    int accept_fail_streak_{0};        // accept 连续失败计数(退避用)
    bool sq_fail_logged_{false};       // SQ 耗尽只记一次日志

    // 声明顺序即析构逆序: workers_ 先 join, outbox_ 后释放,
    // 保证 worker 存活期间 outbox_ 始终有效。
    WorkerOutbox outbox_;
    bike::server::ThreadPool workers_;
};

} // namespace bike::gateway
