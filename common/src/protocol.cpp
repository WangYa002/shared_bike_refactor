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

inline void put_u32_le(std::vector<std::uint8_t>& out, std::uint32_t u) {
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

inline std::uint32_t get_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
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
    put_u32_le(out, f.seq);
    put_i32_le(out, static_cast<std::int32_t>(f.payload.size()));
    out.insert(out.end(),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()) + f.payload.size());
    return out;
}

DecodeOutcome decode_frame(const std::uint8_t* buf, std::size_t n) {
    DecodeOutcome out;
    if (n < kHeaderLen) {
        out.status = DecodeStatus::NeedMore;
        return out;
    }
    if (std::memcmp(buf, kFrameMagic, 4) != 0) {
        out.status = DecodeStatus::BadFrame;
        return out;
    }
    std::int32_t len = get_i32_le(buf + 10);
    if (len < 0 || static_cast<std::uint32_t>(len) > kMaxMessageLen) {
        out.status = DecodeStatus::BadFrame;
        return out;
    }
    std::size_t total = kHeaderLen + static_cast<std::size_t>(len);
    if (n < total) {
        out.status = DecodeStatus::NeedMore;
        return out;
    }
    out.status = DecodeStatus::Ok;
    out.frame.event_id = get_u16_le(buf + 4);
    out.frame.seq = get_u32_le(buf + 6);
    out.frame.payload.assign(reinterpret_cast<const char*>(buf + kHeaderLen),
                             static_cast<std::size_t>(len));
    out.consumed = total;
    return out;
}

std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n) {
    auto out = decode_frame(buf, n);
    if (out.status != DecodeStatus::Ok) return std::nullopt;
    return DecodeResult{std::move(out.frame), out.consumed};
}

bool stamp_seq(std::vector<std::uint8_t>& encoded, std::uint32_t seq) {
    if (encoded.size() < kHeaderLen) return false;
    if (std::memcmp(encoded.data(), kFrameMagic, 4) != 0) return false;
    encoded[6] = static_cast<std::uint8_t>(seq & 0xFF);
    encoded[7] = static_cast<std::uint8_t>((seq >> 8) & 0xFF);
    encoded[8] = static_cast<std::uint8_t>((seq >> 16) & 0xFF);
    encoded[9] = static_cast<std::uint8_t>((seq >> 24) & 0xFF);
    return true;
}

} // namespace bike
