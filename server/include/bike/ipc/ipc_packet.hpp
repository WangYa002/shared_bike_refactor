#pragma once

// 模块三 IPC 泛化报文单元 (设计稿 §7.1)。
// 承载任意 FBEB 请求/响应; 定长 32B, 全值语义、无指针(位置无关),
// 可整体 memcpy 进共享内存槽位。纯逻辑, 跨平台。

#include <cstdint>

namespace bike::ipc {

// flags 位定义
inline constexpr std::uint16_t kFlagOneWay = 0x1;   // 单向事件(如 0x15), 不期待响应

struct alignas(8) IpcPacket {
    std::uint64_t conn_id{0};      // Gateway 连接 id; 响应环据此回投连接
    std::uint16_t event_id{0};     // FBEB eid (common 枚举常量)
    std::uint16_t flags{0};        // bit0 = kFlagOneWay
    std::uint32_t seq{0};          // 请求 seq 透传; Dispatch 用它 stamp 响应帧
    std::uint32_t payload_len{0};  // 槽内联 payload 字节数(紧跟槽头之后)
    std::uint32_t reserved0{0};    // 凑齐 32B, 必须写 0(便于将来扩展字段)
    std::uint32_t reserved1{0};    // 同上
};
static_assert(sizeof(IpcPacket) == 32, "IpcPacket must stay 32B");

} // namespace bike::ipc
