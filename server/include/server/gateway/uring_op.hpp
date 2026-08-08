#pragma once

#include <cstdint>

namespace bike::gateway {

class Connection;

// 每个在途 SQE 对应一个 UringOp, 地址写入 sqe->user_data (类型标记方案)。
// 主线程独占创建/销毁; CQE 到达后按 kind 分发。
// 纯结构体, 无系统调用, 跨平台可编译。
enum class OpKind : std::uint8_t {
    Accept,   // IORING_OP_ACCEPT, conn == nullptr
    Recv,     // IORING_OP_RECV,   conn != nullptr
    Send,     // IORING_OP_SEND,   conn != nullptr
    Wakeup,   // IORING_OP_READ on eventfd(worker→主线程通道), conn == nullptr
    Stop,     // IORING_OP_READ on stop_eventfd(信号→主线程), conn == nullptr
    RspNotify,// IORING_OP_READ on rsp_notify FIFO(Dispatch→主线程, 模块三), conn == nullptr
};

struct UringOp {
    OpKind kind{};
    Connection* conn{nullptr};
    // Wakeup/Stop: eventfd 读值槽位(8 字节), read SQE 的 buffer 指向它
    std::uint64_t event_val{0};
};

} // namespace bike::gateway
