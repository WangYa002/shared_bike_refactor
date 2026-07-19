#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/pricing.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

#include <chrono>
#include <utility>
#include <vector>

namespace bike::server::handlers {

namespace {
long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}

std::vector<std::uint8_t> end_ride(const std::string& payload, Ctx& ctx) {
    tutorial::end_ride_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::end_ride_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);
    if (!valid_lat(req.end_lat()) || !valid_lng(req.end_lng()))
        return fail(ErrCode::InvalidData);

    auto sess = ctx.ride_sessions->find(req.ride_no());
    if (!sess) {
        // 幂等路径:订单已结,查历史
        auto row = ctx.rides->find_by_no(req.ride_no());
        if (!row)                 return fail(ErrCode::InvalidData, "订单不存在");
        if (row->user_id != *uid) return fail(ErrCode::Unauthorized);
        rsp.set_code(code(ErrCode::Ok));
        rsp.set_desc(std::string(to_string(ErrCode::Ok)));
        rsp.set_duration_sec(row->duration_sec);
        rsp.set_distance_m(row->distance_m);
        rsp.set_amount_cent(row->amount_cent);
        rsp.set_balance_after(ctx.accounts->get_balance(*uid));
        Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
        return encode(f);
    }
    if (sess->user_id != *uid) return fail(ErrCode::Unauthorized);

    long long end_ts = now_unix();
    int duration_sec = static_cast<int>(end_ts - sess->start_ts);
    if (duration_sec < 0) duration_sec = 0;
    int amount = compute_fee(duration_sec);
    double dist_m = haversine_m(sess->start_lat, sess->start_lng,
                                 req.end_lat(), req.end_lng());

    int new_bal = ctx.accounts->add_balance(*uid, RecordType::Consume, -amount);
    if (new_bal < 0) {
        return fail(ErrCode::ProcessFailed, "余额不足");
    }

    std::vector<RidePoint> points = {
        {.seq = 0, .lat = sess->start_lat, .lng = sess->start_lng, .elapsed_sec = 0},
        {.seq = sess->last_seq, .lat = req.end_lat(), .lng = req.end_lng(),
         .elapsed_sec = duration_sec},
    };

    CreateRideInput in{
        .ride_no = sess->ride_no, .user_id = *uid, .bike_id = sess->bike_id,
        .start_ts = sess->start_ts, .end_ts = end_ts,
        .start_lat = sess->start_lat, .start_lng = sess->start_lng,
        .end_lat = req.end_lat(), .end_lng = req.end_lng(),
        .duration_sec = duration_sec,
        .distance_m = static_cast<int>(dist_m),
        .amount_cent = amount,
        .points = std::move(points),
    };
    ctx.rides->create_with_points(in);

    ctx.bikes->update_location(sess->bike_id, req.end_lat(), req.end_lng());
    ctx.bikes->update_status(sess->bike_id, BikeStatus::Idle);
    ctx.ride_sessions->remove(sess->ride_no);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_duration_sec(duration_sec);
    rsp.set_distance_m(static_cast<int>(dist_m));
    rsp.set_amount_cent(amount);
    rsp.set_balance_after(new_bal);

    BIKE_LOG_INFO("end_ride ride={} dur={} amt={} bal={}",
                  sess->ride_no, duration_sec, amount, new_bal);
    Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
