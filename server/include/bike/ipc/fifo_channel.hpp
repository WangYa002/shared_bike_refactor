#pragma once

// 模块三 FIFO 唤醒通道 (Linux-only, 设计稿 §6)。
// mkfifo + O_RDWR|O_NONBLOCK 打开(单端打开不阻塞, 无 ENXIO),
// 1 字节写通知 + 读空 drain。纯粘合, 环协议见 spsc_ring.hpp。

#include <string>

namespace bike::ipc {

class FifoChannel {
public:
    FifoChannel() = default;
    FifoChannel(FifoChannel&& o) noexcept;
    FifoChannel& operator=(FifoChannel&& o) noexcept;
    ~FifoChannel();
    FifoChannel(const FifoChannel&) = delete;
    FifoChannel& operator=(const FifoChannel&) = delete;

    // 不存在则 mkfifo(0600), 然后 O_RDWR|O_NONBLOCK 打开。失败抛 runtime_error。
    static FifoChannel create_or_open(const std::string& path);

    int fd() const { return fd_; }
    const std::string& path() const { return path_; }

    // 写 1 字节通知。EAGAIN/EINTR 忽略(通知可合并丢失, 消费方总是先无条件查环)。
    // 其它错误返回 false(调用方记日志即可, 不致命)。
    bool write1();

    // 读空为止(非阻塞), 返回读到的字节数。
    std::size_t drain();

    void close();

private:
    int fd_{-1};
    std::string path_;
};

} // namespace bike::ipc
