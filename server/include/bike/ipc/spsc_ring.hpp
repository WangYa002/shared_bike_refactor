#pragma once

// 模块三 SPSC 无锁环 (设计稿 §5/§7.3)。
// 模板化槽大小/容量; Producer/Consumer 视图严格单线程使用。
// 32 位 head/tail 回绕安全性: 差值 head-tail 按模 2^32 运算,
// 只要在途条目数 < 2^31(环容量 ≤ 4096, 远小于该界)满/空判定恒正确。
// 纯逻辑、无系统调用、跨平台: 单测可在堆内存上直接验证协议。

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "bike/ipc/shm_layout.hpp"

namespace bike::ipc {

template <std::size_t SlotSize, std::uint32_t SlotCountBits>
class SpscRing {
    static_assert(SlotCountBits >= 1 && SlotCountBits <= 16,
                  "slot count must be 2^k with k in [1,16]");

public:
    static constexpr std::size_t   kSlotSize   = SlotSize;
    static constexpr std::uint32_t kSlotCount  = 1u << SlotCountBits;
    static constexpr std::uint32_t kMask       = kSlotCount - 1;
    static constexpr std::size_t   kPayloadMax = SlotSize - sizeof(IpcPacket);
    static constexpr std::size_t   kRegionBytes = SlotSize * kSlotCount;
    using Slot = RingSlotT<SlotSize>;

    // producer 视图: head 的唯一写者。
    // 注意: head_local_ 是"预订指针", begin_write 即推进;
    // 共享 head 只在 publish 时更新, 故 size()/full() 含已预订未发布槽。
    class Producer {
    public:
        Producer() = default;
        Producer(RingHeader* h, Slot* s)
            : hdr_(h), slots_(s),
              head_local_(h->head.load(std::memory_order_relaxed)),
              head_published_(head_local_) {}

        bool valid() const { return hdr_ != nullptr; }

        // ---- 批量两阶段写入 ----
        // 阶段一: 预订 1 个槽并推进本地预订指针(不写共享 head, 可连续预订
        // 多次再一次性 publish)。返回 nullptr = 环满(已预订+在途 == 容量)。
        // tail 用 acquire 读: 配对 consumer release, 确认槽已归还。
        Slot* begin_write() {
            const std::uint32_t tail_snap = hdr_->tail.load(std::memory_order_acquire);
            if (head_local_ - tail_snap == kSlotCount) return nullptr;
            ++unpublished_;
            return &slots_[head_local_++ & kMask];
        }

        // 就地填充槽位(pkt 头 + payload memcpy), 不发布。
        static void fill(Slot* slot, const IpcPacket& pkt, const void* payload) {
            slot->pkt = pkt;
            if (pkt.payload_len > 0)
                std::memcpy(slot->payload, payload, pkt.payload_len);
        }

        // 阶段二: 发布已预订并顺序填充的前 n 个槽。契约: n 必须等于未发布
        // 预订次数(断言固化); 只发布实际填充的 n 个 —— 未发布的尾部预订
        // 保留在 head_local_ 中, 由后续 publish 补齐, 不会暴露给 consumer。
        // release: 槽内全部普通写对执行配对 acquire 的 consumer 可见。
        void publish(std::uint32_t n) {
            assert(n == unpublished_ && "publish(n): n must match outstanding reservations");
            unpublished_ -= n;
            head_published_ += n;
            hdr_->head.store(head_published_, std::memory_order_release);
        }

        // ---- 单条便捷接口 ----
        bool push(const IpcPacket& pkt, const void* payload) {
            Slot* slot = begin_write();
            if (slot == nullptr) return false;
            fill(slot, pkt, payload);
            publish(1);
            return true;
        }

        bool full() const {
            const std::uint32_t tail_snap = hdr_->tail.load(std::memory_order_acquire);
            return head_local_ - tail_snap == kSlotCount;
        }
        std::uint32_t size() const {
            const std::uint32_t tail_snap = hdr_->tail.load(std::memory_order_acquire);
            return head_local_ - tail_snap;
        }
        bool empty() const { return size() == 0; }

        // 判活/心跳(设计稿 §5.7): pid 注册 + 每 1s 由所有者调用
        void register_pid(std::uint32_t pid) {
            hdr_->producer_pid.store(pid, std::memory_order_relaxed);
        }
        void heartbeat(std::uint64_t now_ns) {
            hdr_->producer_heartbeat_ns.store(now_ns, std::memory_order_relaxed);
        }
        std::uint32_t peer_pid() const {
            return hdr_->consumer_pid.load(std::memory_order_relaxed);
        }

    private:
        RingHeader* hdr_{nullptr};
        Slot* slots_{nullptr};
        std::uint32_t head_local_{0};      // 预订指针: 只有本线程写, 本地缓存免 atomic 往返
        std::uint32_t head_published_{0};  // 已发布指针(共享 head 镜像)
        std::uint32_t unpublished_{0};     // 已预订未发布计数(publish 断言用)
    };

    // consumer 视图: tail 的唯一写者
    class Consumer {
    public:
        Consumer() = default;
        Consumer(RingHeader* h, Slot* s)
            : hdr_(h), slots_(s),
              tail_local_(h->tail.load(std::memory_order_relaxed)) {}

        bool valid() const { return hdr_ != nullptr; }

        // 一次 acquire 读取: 返回可读槽数(≤max), *out 指向首个可读槽。
        // 为保证物理连续, 批次不跨越数组回绕点(尾部截断), 下一轮再取。
        std::uint32_t pop_batch(const Slot** out, std::uint32_t max) {
            const std::uint32_t head_snap = hdr_->head.load(std::memory_order_acquire);
            std::uint32_t n = std::min(max, head_snap - tail_local_);
            const std::uint32_t to_end = kSlotCount - (tail_local_ & kMask);
            n = std::min(n, to_end);
            if (n == 0) return 0;
            *out = &slots_[tail_local_ & kMask];
            return n;
        }

        // 归还 n 个已读取的槽。release: 保证读取完成发生在 producer 覆写之前。
        void release(std::uint32_t n) {
            tail_local_ += n;
            hdr_->tail.store(tail_local_, std::memory_order_release);
        }

        bool empty() const {
            const std::uint32_t head_snap = hdr_->head.load(std::memory_order_acquire);
            return head_snap == tail_local_;
        }

        void register_pid(std::uint32_t pid) {
            hdr_->consumer_pid.store(pid, std::memory_order_relaxed);
        }
        void heartbeat(std::uint64_t now_ns) {
            hdr_->consumer_heartbeat_ns.store(now_ns, std::memory_order_relaxed);
        }
        // 对端(producer)判活素材
        std::uint64_t producer_heartbeat() const {
            return hdr_->producer_heartbeat_ns.load(std::memory_order_relaxed);
        }
        std::uint32_t producer_pid() const {
            return hdr_->producer_pid.load(std::memory_order_relaxed);
        }

    private:
        RingHeader* hdr_{nullptr};
        Slot* slots_{nullptr};
        std::uint32_t tail_local_{0};
    };
};

// v1 编译期实例化(与设计稿 §4.6 容量预算一致; [ipc] 槽数配置必须与之一致):
using ReqRing = SpscRing<kReqSlotSize, 9>;   // 512 槽/环, 单环 2MB
using RspRing = SpscRing<kRspSlotSize, 8>;   // 256 槽, 单环 ~98MB

} // namespace bike::ipc
