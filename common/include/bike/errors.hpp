#pragma once

#include <string_view>

namespace bike {

enum class ErrCode : int {
    Ok               = 200,
    InvalidMsg       = 400,
    Unauthorized     = 401,
    InvalidData      = 404,
    MethodNotAllowed = 405,
    ProcessFailed    = 406,
    BikeIsTook       = 407,
    BikeIsRunning    = 408,
    BikeIsDamaged    = 409,
    Unknown          = 0xFF,
};

inline constexpr int code(ErrCode c) { return static_cast<int>(c); }

constexpr std::string_view to_string(ErrCode c) {
    switch (c) {
        case ErrCode::Ok:               return "ok";
        case ErrCode::InvalidMsg:       return "invalid message";
        case ErrCode::Unauthorized:     return "unauthorized (session expired)";
        case ErrCode::InvalidData:      return "invalid data";
        case ErrCode::MethodNotAllowed: return "method not allowed";
        case ErrCode::ProcessFailed:    return "process failed";
        case ErrCode::BikeIsTook:       return "bike is took";
        case ErrCode::BikeIsRunning:    return "bike is running";
        case ErrCode::BikeIsDamaged:    return "bike is damaged";
        case ErrCode::Unknown:          return "unknown";
    }
    return "unknown";
}

} // namespace bike
