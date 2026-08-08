#pragma once

// 模块三 共享内存区域管理 (Linux-only, 设计稿 §5.6/§5.7)。
// shm_open + ftruncate + mmap, 文件落 /dev/shm (POSIX shm 语义);
// magic/version 校验 + 双 pid 心跳的创建/附着/崩溃恢复。
// 纯逻辑环见 spsc_ring.hpp (跨平台)。

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "bike/ipc/spsc_ring.hpp"

namespace bike::ipc {

// 一对 shm 文件(req: N 个请求环; rsp: 1 个响应环)的映射与视图工厂。
// 创建方(Dispatch): create_or_recover —— 残留文件按 §5.7 策略表重建。
// 打开方(Gateway):  open —— 重试等待 Dispatch 建环, 超时抛异常(fail-fast)。
class ShmRegion {
public:
    struct Params {
        std::string shm_root{"/dev/shm"};   // FIFO 目录(ring 文件固定 /dev/shm)
        std::string prefix{"bike"};
        int instance{0};
        int workers{8};                     // 请求环个数 N(双进程必须一致)
        std::chrono::milliseconds open_timeout{10000};
    };

    ShmRegion(ShmRegion&&) noexcept;
    ShmRegion& operator=(ShmRegion&&) noexcept;
    ~ShmRegion();
    ShmRegion(const ShmRegion&) = delete;
    ShmRegion& operator=(const ShmRegion&) = delete;

    static ShmRegion create_or_recover(const Params& p);   // Dispatch 侧
    static ShmRegion open(const Params& p);                // Gateway 侧(重试)

    // attach 视图(调用方各自保证单线程使用约束)
    std::vector<ReqRing::Consumer> attach_req_consumers();   // Dispatch
    std::vector<ReqRing::Producer> attach_req_producers();   // Gateway
    RspRing::Producer attach_rsp_producer();                 // Dispatch
    RspRing::Consumer attach_rsp_consumer();                 // Gateway

    // FIFO 路径({shm_root}/{prefix}{instance}_*_notify)
    std::string req_notify_path() const;
    std::string rsp_notify_path() const;

    const Params& params() const { return p_; }
    std::size_t req_total_bytes() const;
    std::size_t rsp_total_bytes() const;

    static bool pid_alive(std::uint32_t pid);

private:
    ShmRegion() = default;

    struct MappedFile {
        int fd{-1};
        void* base{nullptr};
        std::size_t bytes{0};
        std::string shm_name;
    };

    // creator_side = true 时允许恢复/重建; false 只校验(失败即抛)
    void map_all(bool creator_side);
    void unmap_all();
    // 校验已映射文件的头; 返回是否可直接使用
    static bool validate_file(const MappedFile& mf, std::uint32_t ring_count,
                              std::uint32_t slot_count, std::size_t slot_size,
                              std::size_t total);
    // 创建方重建残留文件: ftruncate + 重写全部头
    static void reinit_file(const MappedFile& mf, std::uint32_t ring_count,
                            std::uint32_t slot_count, std::size_t slot_size,
                            std::size_t total);
    static bool shm_create(const std::string& name, std::size_t total,
                           MappedFile& out);                 // false = EEXIST
    static void shm_open_existing(const std::string& name, MappedFile& out); // 失败抛异常

    Params p_{};
    MappedFile req_;
    MappedFile rsp_;
};

} // namespace bike::ipc
