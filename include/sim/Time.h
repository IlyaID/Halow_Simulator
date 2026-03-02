#pragma once
#include <cstdint>

namespace sim {

using TimeUs = int64_t;

constexpr TimeUs US(TimeUs x) { return x; }
constexpr TimeUs MS(TimeUs x) { return x * 1000; }
constexpr TimeUs S (TimeUs x) { return x * 1000 * 1000; }

}
