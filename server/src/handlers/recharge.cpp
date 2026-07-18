#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> recharge(const std::string& payload, Ctx& ctx) {
    tutorial::recharge_response rsp;

    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        rsp.set_desc(std::string(to_string(ec)));
        Frame f{.event_id = 0x06, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::recharge_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    if (req.amount() <= 0)
        return fail(ErrCode::InvalidData);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    int new_bal = ctx.accounts->add_balance(u.id, RecordType::Recharge, req.amount());
    BIKE_LOG_INFO("recharge mobile={} amount={} bal={}", *mobile, req.amount(), new_bal);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_balance(new_bal);
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    Frame f{.event_id = 0x06, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
