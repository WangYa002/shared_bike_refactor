#include "server/handlers.hpp"
#include "server/auth.hpp"

#include <bike.pb.h>

#include <ctime>
#include <string>

namespace bike::server::handlers {

namespace {
std::string fmt_iso(long long unix_sec) {
    auto t = static_cast<std::time_t>(unix_sec);
    std::tm tm = *std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string{buf};
}
}

std::vector<std::uint8_t> get_ride_detail(const std::string& payload, Ctx& ctx) {
    tutorial::get_ride_detail_response rsp;
    auto fail = [&](ErrCode ec, std::string /*desc*/ = "") {
        rsp.set_code(code(ec));
        Frame f{.event_id = event_id(Event::GetRideDetailResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::get_ride_detail_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    auto ride = ctx.rides->find_by_no(req.ride_no());
    if (!ride)                 return fail(ErrCode::InvalidData, "订单不存在");
    if (ride->user_id != *uid) return fail(ErrCode::Unauthorized);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_ride_no(ride->ride_no);
    rsp.set_duration_sec(ride->duration_sec);
    rsp.set_distance_m(ride->distance_m);
    rsp.set_amount_cent(ride->amount_cent);
    rsp.set_start_tm(fmt_iso(ride->start_ts));
    rsp.set_end_tm(fmt_iso(ride->end_ts));

    auto points = ctx.rides->list_points(ride->id);
    for (const auto& p : points) {
        auto* pp = rsp.add_points();
        pp->set_lat(p.lat);
        pp->set_lng(p.lng);
        pp->set_elapsed_sec(p.elapsed_sec);
    }
    Frame f{.event_id = event_id(Event::GetRideDetailResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
