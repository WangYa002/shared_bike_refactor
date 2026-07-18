#pragma once

#include <bike/errors.hpp>
#include <bike/protocol.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "server/router.hpp"

namespace bike::server::handlers {

std::vector<std::uint8_t> mobile_code(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> login(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> recharge(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> account_balance(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> list_records(const std::string& payload, Ctx& ctx);

} // namespace bike::server::handlers
