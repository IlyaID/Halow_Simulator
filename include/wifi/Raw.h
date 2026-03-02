#pragma once
#include "../sim/Time.h"
#include "../sim/Simulator.h"
#include "Types.h"

namespace wifi {

// Упрощённый RAW: каждый beaconInterval идёт RAW с numSlots слотов фикс. длительности.
// STA с id попадает в слот (id % numSlots).
struct RawConfig {
  bool enabled{true};
  sim::TimeUs beaconIntervalUs{sim::MS(1024)};
  sim::TimeUs rawStartOffsetUs{sim::US(2000)};
  sim::TimeUs slotUs{sim::US(20000)};
  int numSlots{8};
  bool csbEnabled{true}; // cross-slot boundary
};

class RawGate {
public:
  RawGate(NodeId staId, RawConfig cfg) : c(cfg), id(staId) {}

  bool IsInMySlot(sim::TimeUs t) const {
    if (!c.enabled) return true;
    auto bi = c.beaconIntervalUs;
    if (bi <= 0) return true;
    auto base = (t / bi) * bi;
    auto rawStart = base + c.rawStartOffsetUs;
    auto rawEnd = rawStart + sim::TimeUs(c.numSlots) * c.slotUs;
    if (t < rawStart || t >= rawEnd) return false;
    int slot = int((t - rawStart) / c.slotUs);
    int my = (c.numSlots > 0) ? (id % c.numSlots) : 0;
    return slot == my;
  }

  sim::TimeUs SlotEnd(sim::TimeUs t) const {
    auto bi = c.beaconIntervalUs;
    auto base = (t / bi) * bi;
    auto rawStart = base + c.rawStartOffsetUs;
    int slot = int((t - rawStart) / c.slotUs);
    return rawStart + sim::TimeUs(slot + 1) * c.slotUs;
  }

  bool CanContend(sim::TimeUs t) const { return IsInMySlot(t); }

  bool CanStartTx(sim::TimeUs t, sim::TimeUs txExchangeUs) const {
    if (!c.enabled) return true;
    if (!IsInMySlot(t)) return false;
    if (c.csbEnabled) return true;
    return (t + txExchangeUs) <= SlotEnd(t);
  }

  RawConfig c;
  NodeId id{-1};
};

} // namespace wifi
