#pragma once

#include "server/router.hpp"   // 拿到 Ctx

#include <optional>
#include <string>

namespace bike::server {

// 验证 session_token 并返回 user_id。失败返回 nullopt。
// 内部调用 ctx.sessions->lookup_session(token),再用 ctx.users->find_or_create(mobile) 拿 id。
std::optional<int> require_user(const std::string& token, Ctx& ctx);

} // namespace bike::server
