#include <bike/protocol.hpp>

#include <cstring>
#include <stdexcept>

namespace bike {

namespace {

inline void put_u16_le(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

inline void put_i32_le(std::vector<std::uint8_t>& out, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
}

inline std::uint16_t get_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::int32_t get_i32_le(const std::uint8_t* p) {
    auto u = static_cast<std::uint32_t>(p[0]) |
             (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) |
             (static_cast<std::uint32_t>(p[3]) << 24);
    return static_cast<std::int32_t>(u);
}

} // namespace

std::vector<std::uint8_t> encode(const Frame& f) {
    if (f.payload.size() > kMaxMessageLen) {
        throw std::overflow_error("payload exceeds kMaxMessageLen");
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderLen + f.payload.size());
    out.insert(out.end(), kFrameMagic, kFrameMagic + 4);
    put_u16_le(out, f.event_id);
    put_i32_le(out, static_cast<std::int32_t>(f.payload.size()));
    out.insert(out.end(),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()) + f.payload.size());
    return out;
}

std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n) {
    if (n < kHeaderLen) return std::nullopt;
    if (std::memcmp(buf, kFrameMagic, 4) != 0) return std::nullopt;
    std::uint16_t eid = get_u16_le(buf + 4);
    std::int32_t len = get_i32_le(buf + 6);
    if (len < 0 || static_cast<std::uint32_t>(len) > kMaxMessageLen) return std::nullopt;
    std::size_t total = kHeaderLen + static_cast<std::size_t>(len);
    if (n < total) return std::nullopt;
    DecodeResult r;
    r.frame.event_id = eid;
    r.frame.payload.assign(reinterpret_cast<const char*>(buf + kHeaderLen),
                           static_cast<std::size_t>(len));
    r.consumed = total;
    return r;
}

} // namespace bike
