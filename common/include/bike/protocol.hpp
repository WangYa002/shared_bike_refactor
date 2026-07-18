#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike {

inline constexpr char kFrameMagic[4] = {'F', 'B', 'E', 'B'};
inline constexpr std::uint32_t kHeaderLen = 10;
inline constexpr std::uint32_t kMaxMessageLen = 372680;

struct Frame {
    std::uint16_t event_id{0};
    std::string payload;
};

std::vector<std::uint8_t> encode(const Frame& f);

struct DecodeResult {
    Frame frame;
    std::size_t consumed{0};
};
std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n);

} // namespace bike
