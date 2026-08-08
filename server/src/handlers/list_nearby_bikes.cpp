#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

#include <cmath>

namespace bike::server::handlers {

std::vector<std::uint8_t> list_nearby_bikes(const std::string& payload, Ctx& ctx) {
    tutorial::list_nearby_bikes_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = event_id(Event::ListNearbyBikesResponse), .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_nearby_bikes_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    if (!valid_lat(req.lat()) || !valid_lng(req.lng()))
        return fail(ErrCode::InvalidData);

    double r = clamp_radius(req.radius_m());
    double dlat = r / 111000.0;
    double dlng = r / (111000.0 * std::cos(req.lat() * 3.14159265358979 / 180.0));

    auto bikes = ctx.bikes->list_in_bounds(
        req.lat() - dlat, req.lat() + dlat,
        req.lng() - dlng, req.lng() + dlng);

    rsp.set_code(code(ErrCode::Ok));
    for (const auto& b : bikes) {
        double d = haversine_m(req.lat(), req.lng(), b.lat, b.lng);
        if (d > r) continue;
        auto* bi = rsp.add_bikes();
        bi->set_bike_no(b.bike_no);
        bi->set_lat(b.lat);
        bi->set_lng(b.lng);
        bi->set_status(static_cast<int>(b.status));
    }
    Frame f{.event_id = event_id(Event::ListNearbyBikesResponse), .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
