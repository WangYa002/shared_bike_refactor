#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> mobile_code(const std::string& payload, Ctx& ctx) {
    tutorial::mobile_response rsp;

    auto finish = [&](ErrCode ec, int icode) {
        rsp.set_code(code(ec));
        rsp.set_icode(icode);
        rsp.set_data(std::string(to_string(ec)));
        Frame f{.event_id = 0x02, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::mobile_request req;
    if (!req.ParseFromArray(payload.data(), payload.size())) {
        return finish(ErrCode::InvalidMsg, 0);
    }
    int icode = ctx.sessions->set_code(req.mobile());
    BIKE_LOG_INFO("mobile_code mobile={} code={}", req.mobile(), icode);
    return finish(ErrCode::Ok, icode);
}

} // namespace bike::server::handlers
