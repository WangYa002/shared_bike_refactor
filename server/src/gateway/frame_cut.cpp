#include "server/gateway/frame_cut.hpp"

namespace bike::gateway {

CutResult cut_frames(const std::uint8_t* p, std::size_t n,
                     std::vector<bike::Frame>& out) {
    CutResult r;
    std::size_t off = 0;
    while (off < n) {
        auto res = bike::decode_frame(p + off, n - off);
        if (res.status == bike::DecodeStatus::NeedMore) break;
        if (res.status == bike::DecodeStatus::BadFrame) {
            r.bad = true;
            break;
        }
        out.push_back(std::move(res.frame));
        off += res.consumed;
    }
    r.leftover = p + off;
    r.leftover_len = n - off;
    return r;
}

} // namespace bike::gateway
