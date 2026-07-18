#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> login(const std::string& payload, Ctx& ctx) {
    tutorial::login_response rsp;

    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        rsp.set_desc(std::string(to_string(ec)));
        Frame f{.event_id = 0x04, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::login_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto stored = ctx.sessions->get_code(req.mobile());
    if (!stored || *stored != req.icode())
        return fail(ErrCode::InvalidData);

    ctx.sessions->delete_code(req.mobile());
    User u = ctx.users->find_or_create(req.mobile());
    (void)ctx.accounts->get_balance(u.id);

    std::string token = ctx.sessions->create_session(req.mobile());
    BIKE_LOG_INFO("login ok mobile={} uid={}", req.mobile(), u.id);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_session_token(token);
    Frame f{.event_id = 0x04, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
