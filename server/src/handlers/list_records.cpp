#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> list_records(const std::string& payload, Ctx& ctx) {
    tutorial::list_account_records_response rsp;

    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x10, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_account_records_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    auto recs = ctx.accounts->list_records(u.id, 100);
    rsp.set_code(code(ErrCode::Ok));
    for (const auto& r : recs) {
        auto* pr = rsp.add_records();
        pr->set_type(r.type);
        pr->set_amount(r.amount);
        pr->set_timestamp(r.timestamp);
    }
    Frame f{.event_id = 0x10, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
