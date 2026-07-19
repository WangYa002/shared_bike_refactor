#include "server/auth.hpp"

namespace bike::server {

std::optional<int> require_user(const std::string& token, Ctx& ctx) {
    auto mobile = ctx.sessions->lookup_session(token);
    if (!mobile) return std::nullopt;
    User u = ctx.users->find_or_create(*mobile);
    return u.id;
}

} // namespace bike::server
