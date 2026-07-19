#include "bike/pricing.hpp"

#include <stdexcept>

namespace bike {

int compute_fee(int duration_sec) {
    if (duration_sec < 0) throw std::invalid_argument{"negative duration"};
    constexpr int base_sec = 15 * 60;
    constexpr int step_sec = 15 * 60;
    int fee = 100;
    if (duration_sec <= base_sec) return fee;
    int extra_sec = duration_sec - base_sec;
    int extra_chunks = (extra_sec + step_sec - 1) / step_sec;
    return fee + extra_chunks * 50;
}

} // namespace bike
