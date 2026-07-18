#pragma once

#include <optional>
#include <string>

namespace bike::server {

struct User {
    int id{0};
    std::string mobile;
};

class IUserRepo {
public:
    virtual ~IUserRepo() = default;
    virtual std::optional<User> find_by_mobile(const std::string& mobile) = 0;
    virtual User find_or_create(const std::string& mobile) = 0;
};

} // namespace bike::server
