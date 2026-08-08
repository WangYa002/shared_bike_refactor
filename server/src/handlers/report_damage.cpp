#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> report_damage(const std::string& payload, Ctx& ctx) {
    tutorial::report_damage_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = event_id(Event::ReportDamageResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::report_damage_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);
    if (!valid_bike_no(req.bike_no())) return fail(ErrCode::InvalidData);

    auto bike = ctx.bikes->get_for_update(req.bike_no());
    if (!bike) return fail(ErrCode::InvalidData, "车辆不存在");

    ctx.bikes->update_status(bike->id, BikeStatus::Damaged);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    Frame f{.event_id = event_id(Event::ReportDamageResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
