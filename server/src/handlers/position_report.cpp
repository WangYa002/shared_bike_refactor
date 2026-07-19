#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

// 单向事件,无响应。返回空 vector(router 不会向 client 发回任何字节)。
std::vector<std::uint8_t> position_report(const std::string& payload, Ctx& ctx) {
    tutorial::ride_position_report req;
    if (!req.ParseFromArray(payload.data(), payload.size())) return {};
    ctx.ride_sessions->update_pos(req.ride_no(), req.lat(), req.lng(), req.seq());
    return {};
}

} // namespace bike::server::handlers
