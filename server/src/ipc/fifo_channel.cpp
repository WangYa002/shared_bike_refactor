// 模块三 FIFO 唤醒通道实现 (Linux-only, 设计稿 §6)。

#include "bike/ipc/fifo_channel.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace bike::ipc {

FifoChannel FifoChannel::create_or_open(const std::string& path) {
    if (::mkfifo(path.c_str(), 0600) < 0 && errno != EEXIST)
        throw std::runtime_error("ipc fifo: mkfifo(" + path + ") failed: " +
                                 std::strerror(errno));
    // O_RDWR: 单进程单端打开也保持"写端存在", read 永不 EOF、open 不阻塞;
    // O_NONBLOCK: open/read/write 全不阻塞。
    const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("ipc fifo: open(" + path + ") failed: " +
                                 std::strerror(errno));
    FifoChannel ch;
    ch.fd_ = fd;
    ch.path_ = path;
    return ch;
}

bool FifoChannel::write1() {
    if (fd_ < 0) return false;
    const char b = 1;
    for (;;) {
        const ssize_t n = ::write(fd_, &b, 1);
        if (n == 1) return true;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // 通知可合并
        return false;
    }
}

std::size_t FifoChannel::drain() {
    if (fd_ < 0) return 0;
    std::size_t total = 0;
    char buf[64];
    for (;;) {
        const ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            total += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return total;   // EAGAIN(空)/EOF/错误 均结束
    }
}

void FifoChannel::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

FifoChannel::FifoChannel(FifoChannel&& o) noexcept
    : fd_(o.fd_), path_(std::move(o.path_)) {
    o.fd_ = -1;
}

FifoChannel& FifoChannel::operator=(FifoChannel&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        path_ = std::move(o.path_);
        o.fd_ = -1;
    }
    return *this;
}

FifoChannel::~FifoChannel() {
    close();
}

} // namespace bike::ipc
