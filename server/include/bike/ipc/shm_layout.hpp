#pragma once

// 模块三 共享内存布局结构 (设计稿 §4/§7.2)。
// ShmFileHeader / RingHeader / RingSlotT 均定长定对齐;
// head/tail 按设计要求为 std::atomic<uint32_t> 且分属不同缓存行(消除伪共享)。
// 纯结构定义, 跨平台可编译(单测直接在堆内存上验证无锁协议)。

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "bike/ipc/ipc_packet.hpp"

namespace bike::ipc {

// 跨进程无锁协议的编译期前提: 原子字段必须无锁、报文必须可平凡拷贝
// (整体 memcpy 进 shm 语义成立)。
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "atomic<uint32_t> must be lock-free for cross-process rings");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "atomic<uint64_t> must be lock-free for cross-process rings");
static_assert(std::is_trivially_copyable_v<IpcPacket>,
              "IpcPacket must be trivially copyable (memcpy into shm)");

inline constexpr std::uint32_t kShmMagic   = 0x454B4942; // "BIKE"(LE 读序)
inline constexpr std::uint32_t kShmVersion = 1;          // 环布局版本, 不匹配即重建

inline constexpr std::size_t kCacheLine = 64;
inline constexpr std::size_t kPageAlign = 4096;

// 槽大小(v1 编译期常量, 不可配置, 避免双进程不一致; 论证见设计稿 §4.5/§4.6):
inline constexpr std::size_t kReqSlotSize = 4096;        // 请求环: 内联上限 4064B
inline constexpr std::size_t kRspSlotSize = 401408;      // 响应环: 392KB = 32 + 372680 (4K 对齐)
inline constexpr std::size_t kReqPayloadMax = kReqSlotSize - sizeof(IpcPacket);
inline constexpr std::size_t kRspPayloadMax = kRspSlotSize - sizeof(IpcPacket);

// 文件级元数据头(128B, 独立于任何环): 供打开方在 attach 前完成全部校验。
struct alignas(128) ShmFileHeader {
    std::uint32_t magic{0};
    std::uint32_t version{0};
    std::uint32_t ring_count{0};    // req 文件 = N(worker 数); rsp 文件 = 1
    std::uint32_t slot_count{0};    // 每环槽数(2 的幂)
    std::uint32_t slot_size{0};
    std::uint32_t reserved0{0};
    std::uint64_t total_bytes{0};   // 文件全长(fstat 一致性校验)
    std::uint64_t created_ns{0};    // steady_clock 创建时刻
    std::uint8_t  pad[80];
};
static_assert(sizeof(ShmFileHeader) == 128, "ShmFileHeader must stay 128B");

// 环头(128B): producer 域与 consumer 域各占一条缓存行 —— 消除 head/tail 伪共享。
// pad 取 40B: 字段实际占 16B(atomic<uint32> 4B 对齐)或 24B(8B 对齐),
// 两种情形下 tail 的 alignas(64) 都会对齐到行首, sizeof 恒 128。
static_assert(alignof(std::atomic<std::uint32_t>) <= 8 &&
              alignof(std::atomic<std::uint64_t>) <= 8,
              "unexpected atomic alignment breaks RingHeader layout");
struct RingHeader {
    // ---- producer 独占写域 (缓存行 1, 占 24B) ----
    alignas(kCacheLine) std::atomic<std::uint32_t> head{0};   // 已发布槽位计数
    std::atomic<std::uint32_t> producer_pid{0};
    std::atomic<std::uint64_t> producer_heartbeat_ns{0};      // 每 1s 更新
    std::uint8_t pad0[kCacheLine - 24];

    // ---- consumer 独占写域 (缓存行 2, 占 24B) ----
    alignas(kCacheLine) std::atomic<std::uint32_t> tail{0};   // 已消费槽位计数
    std::atomic<std::uint32_t> consumer_pid{0};
    std::atomic<std::uint64_t> consumer_heartbeat_ns{0};
    std::uint8_t pad1[kCacheLine - 24];
};
static_assert(sizeof(RingHeader) == 128, "RingHeader must stay 128B");
// head/tail 必须落在不同缓存行(伪共享规避的编译期证明)
static_assert(offsetof(RingHeader, tail) - offsetof(RingHeader, head) >= kCacheLine,
              "head/tail must live on distinct cache lines");

// 槽位 = IpcPacket(32B) + 内联 payload。定长: 下标即偏移, 无分配器、无间接层。
template <std::size_t SlotSize>
struct RingSlotT {
    static_assert(SlotSize > sizeof(IpcPacket) && SlotSize % kCacheLine == 0,
                  "slot size must exceed IpcPacket and be cache-line multiple");
    IpcPacket pkt;
    std::uint8_t payload[SlotSize - sizeof(IpcPacket)];
};
static_assert(sizeof(RingSlotT<kReqSlotSize>) == kReqSlotSize);
static_assert(sizeof(RingSlotT<kRspSlotSize>) == kRspSlotSize);

// ---- 区域布局计算(创建方/打开方共用, 纯函数) ----

inline constexpr std::size_t align_up(std::size_t v, std::size_t align) {
    return (v + align - 1) / align * align;
}

// 一个 shm 文件的布局: ShmFileHeader(128B) + RingHeader[ring_count]
// + (4K 对齐) 各环槽位区连续排布。
struct RingFileLayout {
    std::size_t headers_offset{0};   // RingHeader[0] 偏移
    std::size_t slots_offset{0};     // 环 0 槽位区偏移(页对齐)
    std::size_t ring_stride{0};      // 相邻环槽位区字节跨度
    std::size_t total_bytes{0};      // 文件全长
};

inline RingFileLayout compute_file_layout(std::uint32_t ring_count,
                                          std::size_t bytes_per_ring) {
    RingFileLayout l;
    l.headers_offset = sizeof(ShmFileHeader);
    l.slots_offset = align_up(sizeof(ShmFileHeader) +
                                  static_cast<std::size_t>(ring_count) * sizeof(RingHeader),
                              kPageAlign);
    l.ring_stride = bytes_per_ring;
    l.total_bytes = l.slots_offset +
                    static_cast<std::size_t>(ring_count) * bytes_per_ring;
    return l;
}

} // namespace bike::ipc
