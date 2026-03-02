#pragma once
#include "Types.h"
#include <cmath>

namespace wifi {

// Pr(dBm) = Pt(dBm) - (pl0 + 10*n*log10(d/d0))
struct LogDistancePropagation {
  double pl0_db{40.0};
  double n{3.0};
  double d0{1.0};

  double RxPowerDbm(double txPowerDbm, const Vec2& tx, const Vec2& rx) const {
    double dx = tx.x - rx.x, dy = tx.y - rx.y;
    double d = std::sqrt(dx*dx + dy*dy);
    if (d < d0) d = d0;
    double pl = pl0_db + 10.0 * n * std::log10(d / d0);
    return txPowerDbm - pl;
  }
};

inline double DbmToW(double dbm) { return std::pow(10.0, (dbm - 30.0) / 10.0); }
inline double WToDbm(double w)   { return 10.0 * std::log10(w) + 30.0; }

}
