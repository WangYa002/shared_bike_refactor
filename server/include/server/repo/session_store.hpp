#pragma once

#include <optional>
#include <string>

namespace bike::server {

class ISessionStore {
public:
    virtual ~ISessionStore() = default;
    virtual int set_code(const std::string& mobile) = 0;
    virtual std::optional<int> get_code(const std::string& mobile) = 0;
    virtual void delete_code(const std::string& mobile) = 0;
    virtual std::string create_session(const std::string& mobile) = 0;
    virtual std::optional<std::string> lookup_session(const std::string& token) = 0;
    virtual void destroy_session(const std::string& token) = 0;
};

} // namespace bike::server
