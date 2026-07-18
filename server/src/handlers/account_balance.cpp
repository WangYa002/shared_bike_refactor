#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> account_balance(const std::string& payload, Ctx& ctx) {
    tutorial::account_balance_response rsp;

    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x08, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::account_balance_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    int bal = ctx.accounts->get_balance(u.id);
    rsp.set_code(code(ErrCode::Ok));
    rsp.set_balance(bal);
    Frame f{.event_id = 0x08, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
