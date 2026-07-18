#include "server/router.hpp"

namespace bike::server {

void Router::register_handler(std::uint16_t eid, HandlerFn fn) {
    handlers_[eid] = std::move(fn);
}

std::vector<std::uint8_t> Router::dispatch(std::uint16_t eid,
                                           const std::string& payload,
                                           Ctx& ctx) {
    auto it = handlers_.find(eid);
    if (it == handlers_.end()) return {};
    return it->second(payload, ctx);
}

} // namespace bike::server
