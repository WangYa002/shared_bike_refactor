#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike {

inline constexpr char kFrameMagic[4] = {'F', 'B', 'E', 'B'};
// 帧头布局 (全小端): magic(4) + event_id u16(2) + seq u32(4) + len i32(4) = 14
inline constexpr std::uint32_t kHeaderLen = 14;
inline constexpr std::uint32_t kMaxMessageLen = 372680;

// FBEB 协议事件号全表 (单一事实来源)。
// 请求为奇数, 响应 = 请求 + 1 (特例: ListAccountRecords 0x09 -> 0x10),
// RidePositionReport 为单向事件, 无响应。
enum class Event : std::uint16_t {
    MobileRequest              = 0x01, // 获取验证码
    MobileResponse             = 0x02,
    LoginRequest               = 0x03, // 登录
    LoginResponse              = 0x04,
    RechargeRequest            = 0x05, // 充值
    RechargeResponse           = 0x06,
    AccountBalanceRequest      = 0x07, // 查余额
    AccountBalanceResponse     = 0x08,
    ListAccountRecordsRequest  = 0x09, // 账单列表
    ListAccountRecordsResponse = 0x10,
    ListNearbyBikesRequest     = 0x11, // 附近车辆
    ListNearbyBikesResponse    = 0x12,
    ScanUnlockRequest          = 0x13, // 扫码解锁
    ScanUnlockResponse         = 0x14,
    RidePositionReport         = 0x15, // 位置上报 (单向, 无响应)
    EndRideRequest             = 0x17, // 结束骑行
    EndRideResponse            = 0x18,
    ReportDamageRequest        = 0x19, // 报修
    ReportDamageResponse       = 0x1A,
    GetRideDetailRequest       = 0x1B, // 订单详情
    GetRideDetailResponse      = 0x1C,
    ListRidesRequest           = 0x1D, // 骑行历史
    ListRidesResponse          = 0x1E,
};

inline constexpr std::uint16_t event_id(Event e) {
    return static_cast<std::uint16_t>(e);
}

struct Frame {
    std::uint16_t event_id{0};
    // 请求序号: 客户端每连接自增, 服务端原样回带(不配对不校验)。
    // 放在 payload 之前, 不影响现有 {.event_id=.., .payload=..} 指定初始化。
    std::uint32_t seq{0};
    std::string payload;
};

std::vector<std::uint8_t> encode(const Frame& f);

struct DecodeResult {
    Frame frame;
    std::size_t consumed{0};
};
// 兼容入口: 半包/坏帧统一返回 nullopt。
std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n);

// 三态解码: 区分半包(NeedMore)与坏帧(BadFrame, magic/len 非法),
// 网关 worker 切帧依赖此区分。
enum class DecodeStatus : std::uint8_t { NeedMore, BadFrame, Ok };
struct DecodeOutcome {
    DecodeStatus status{DecodeStatus::NeedMore};
    Frame frame;
    std::size_t consumed{0};
};
DecodeOutcome decode_frame(const std::uint8_t* buf, std::size_t n);

// 对 encode() 产物原位回写 seq 字段(字节偏移 [6,10))。
// 供网关把请求 seq stamp 进 handler 已编码的响应帧, 使 bike_server_core 零改动。
// magic 不符或长度不足返回 false。
bool stamp_seq(std::vector<std::uint8_t>& encoded, std::uint32_t seq);

} // namespace bike
