#pragma once

#include <string>

namespace bike::client {

struct SessionModel {
    std::string mobile;
    std::string token;
    bool logged_in() const { return !token.empty(); }
    void clear() { mobile.clear(); token.clear(); }
};

} // namespace bike::client
