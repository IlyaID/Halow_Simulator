#pragma once
#include <cstdint>

namespace wifi {

using NodeId = int32_t;
static constexpr NodeId BROADCAST = -1;

enum class Ac : int { BK=0, BE=1, VI=2, VO=3 };
enum class FrameType : int { DATA=0, ACK=1, BA=2, BAR=3, BEACON=4 };

struct Vec2 { double x{0}, y{0}; };

}
