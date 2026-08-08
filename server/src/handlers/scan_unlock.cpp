#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/ride_no.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

#include <chrono>
#include <ctime>

namespace bike::server::handlers {

namespace {
long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
int today_yyyymmdd() {
    auto t = std::time(nullptr);
    std::tm tm = *std::gmtime(&t);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}
} // namespace

std::vector<std::uint8_t> scan_unlock(const std::string& payload, Ctx& ctx) {
    tutorial::scan_unlock_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = event_id(Event::ScanUnlockResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::scan_unlock_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    if (!valid_bike_no(req.bike_no()) || !valid_lat(req.lat()) || !valid_lng(req.lng()))
        return fail(ErrCode::InvalidData);

    auto bike = ctx.bikes->get_for_update(req.bike_no());
    if (!bike) return fail(ErrCode::InvalidData, "车辆不存在");

    if (bike->status == BikeStatus::Damaged) return fail(ErrCode::BikeIsDamaged);
    if (bike->status == BikeStatus::Rented)  return fail(ErrCode::BikeIsRunning);

    int bal = ctx.accounts->get_balance(*uid);
    if (bal < 100) return fail(ErrCode::ProcessFailed, "余额不足");

    // ride_no: simplified to unix-based seq for unit-test isolation.
    // Production will swap to Redis INCR-based daily seq via ctx.sessions (TODO in integration).
    int seq = static_cast<int>((now_unix() % 999999) + 1);
    std::string ride_no = make_ride_no(today_yyyymmdd(), seq);

    RideSession s;
    s.ride_no   = ride_no;
    s.user_id   = *uid;
    s.bike_id   = bike->id;
    s.start_lat = req.lat();
    s.start_lng = req.lng();
    s.start_ts  = now_unix();
    s.last_lat  = req.lat();
    s.last_lng  = req.lng();
    s.last_seq  = 0;
    ctx.ride_sessions->create(s);

    ctx.bikes->update_status(bike->id, BikeStatus::Rented);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_ride_no(ride_no);
    rsp.set_start_ts(s.start_ts);
    BIKE_LOG_INFO("scan_unlock user={} bike={} ride={}", *uid, req.bike_no(), ride_no);
    Frame f{.event_id = event_id(Event::ScanUnlockResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
