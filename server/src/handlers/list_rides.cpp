#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "bike/validation.hpp"

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

std::vector<std::uint8_t> list_rides(const std::string& payload, Ctx& ctx) {
    tutorial::list_rides_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = event_id(Event::ListRidesResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_rides_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    int limit = clamp_limit(req.limit());
    auto rides = ctx.rides->list_by_user(*uid, limit);

    rsp.set_code(code(ErrCode::Ok));
    for (const auto& r : rides) {
        auto* s = rsp.add_rides();
        s->set_ride_no(r.ride_no);
        s->set_start_tm(fmt_iso(r.start_ts));
        s->set_duration_sec(r.duration_sec);
        s->set_distance_m(r.distance_m);
        s->set_amount_cent(r.amount_cent);
    }
    Frame f{.event_id = event_id(Event::ListRidesResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
