#pragma once

#include <bike/protocol.hpp>

#include <cstdint>
#include <vector>

namespace bike::gateway {

// 从 [p, p+n) 循环切出完整帧(基于 bike::decode_frame 三态), 纯逻辑, 跨平台可测。
// 返回:
//   bad          — 遇到坏帧(magic/len 非法), 调用方应关闭连接;
//   leftover/len — 尾部未切完的半包(指向原缓冲内部), 由主线程回填 rx 累积区。
struct CutResult {
    bool bad{false};
    const std::uint8_t* leftover{nullptr};
    std::size_t leftover_len{0};
};

CutResult cut_frames(const std::uint8_t* p, std::size_t n,
                     std::vector<bike::Frame>& out);

} // namespace bike::gateway
