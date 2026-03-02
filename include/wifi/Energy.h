#pragma once
#include "../sim/Time.h"
#include "../sim/Simulator.h"

namespace wifi {

enum class RadioState : int { SLEEP=0, IDLE=1, RX=2, TX=3, COLLISION=4 };

struct PowerProfileW {
  double p_sleep{1e-7};
  double p_idle{0.02};
  double p_rx{0.09};
  double p_tx{0.20};
  double p_collision{0.20};
};

class EnergyModel {
public:
  explicit EnergyModel(PowerProfileW p = {}) : prof(p) {}

  void SetState(RadioState s) {
    auto now = sim::Simulator::Instance().Now();
    Accumulate(now);
    state = s;
    last = now;
  }

  double Joules() {
    Accumulate(sim::Simulator::Instance().Now());
    return energyJ;
  }

  RadioState GetState() const { return state; }

private:
  double P(RadioState s) const {
    switch (s) {
      case RadioState::SLEEP: return prof.p_sleep;
      case RadioState::IDLE: return prof.p_idle;
      case RadioState::RX: return prof.p_rx;
      case RadioState::TX: return prof.p_tx;
      case RadioState::COLLISION: return prof.p_collision;
    }
    return prof.p_idle;
  }

  void Accumulate(sim::TimeUs now) {
    if (now <= last) return;
    double dt = double(now - last) * 1e-6;
    energyJ += P(state) * dt;
  }

  PowerProfileW prof;
  RadioState state{RadioState::SLEEP};
  sim::TimeUs last{0};
  double energyJ{0.0};
};

} // namespace wifi
