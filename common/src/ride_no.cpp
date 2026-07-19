#include "bike/ride_no.hpp"

#include <cstdio>
#include <stdexcept>

namespace bike {

std::string make_ride_no(int date_yyyymmdd, int daily_seq) {
    if (daily_seq < 1 || daily_seq > 999999) {
        throw std::invalid_argument{"daily_seq out of [1, 999999]"};
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "R%08d%06d", date_yyyymmdd, daily_seq);
    return std::string{buf};
}

} // namespace bike
