#pragma once
#include "Types.h"
#include "Packet.h"
#include "Propagation.h"
#include "../sim/Simulator.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <algorithm>

namespace wifi {

// “PPDU on air” (упрощённая единица передачи)
struct AirTx {
  uint64_t txId{0};
  NodeId txNode{-1};
  Frame frame{};
  sim::TimeUs tStart{0};
  sim::TimeUs tEnd{0};
  double txPowerDbm{0.0};
};

// Интерфейс слушателя эфира, чтобы Medium не зависел от класса Phy (и не ловил incomplete type).
class IAirListener {
public:
  virtual ~IAirListener() = default;
  virtual NodeId ListenerId() const = 0;
  virtual Vec2   ListenerPos() const = 0;
  virtual void OnAirTxStart(const AirTx& tx, double rxPowerW) = 0;
  virtual void OnAirTxEnd(const AirTx& tx, double rxPowerW) = 0;
};

class Medium {
public:
  explicit Medium(LogDistancePropagation p = {}) : prop(p) {}

  void Register(IAirListener* l) { listeners.push_back(l); }

  void SetNoiseDbm(double dbm) { noiseW = DbmToW(dbm); }
  double NoiseW() const { return noiseW; }

  bool IsBusy(sim::TimeUs now) const { return now < busyUntil; }
  sim::TimeUs BusyUntil() const { return busyUntil; }

  // callback: где находится TX node (для расчёта Prx)
  std::function<Vec2(NodeId)> posOf;

  uint64_t StartTx(NodeId txNode, const Frame& f, sim::TimeUs dur, double txPowerDbm) {
    auto now = sim::Simulator::Instance().Now();
    AirTx tx;
    tx.txId = ++nextId;
    tx.txNode = txNode;
    tx.frame = f;
    tx.tStart = now;
    tx.tEnd = now + dur;
    tx.txPowerDbm = txPowerDbm;
    ongoing[tx.txId] = tx;

    busyUntil = std::max(busyUntil, tx.tEnd);

    // Notify all listeners about TX start
    for (auto* l : listeners) {
      if (!l) continue;
      if (!posOf) continue;
      double rxDbm = prop.RxPowerDbm(txPowerDbm, posOf(txNode), l->ListenerPos());
      l->OnAirTxStart(tx, DbmToW(rxDbm));
    }

    sim::Simulator::Instance().Schedule(tx.tEnd, [this, id=tx.txId]{ this->EndTx(id); });
    return tx.txId;
  }

  const LogDistancePropagation& Prop() const { return prop; }
  LogDistancePropagation& Prop() { return prop; }

private:
  void EndTx(uint64_t txId) {
    auto it = ongoing.find(txId);
    if (it == ongoing.end()) return;
    AirTx tx = it->second;
    ongoing.erase(it);

    // Notify all listeners about TX end
    for (auto* l : listeners) {
      if (!l) continue;
      if (!posOf) continue;
      double rxDbm = prop.RxPowerDbm(tx.txPowerDbm, posOf(tx.txNode), l->ListenerPos());
      l->OnAirTxEnd(tx, DbmToW(rxDbm));
    }

    auto now = sim::Simulator::Instance().Now();
    sim::TimeUs newBusy = now;
    for (auto& kv : ongoing) newBusy = std::max(newBusy, kv.second.tEnd);
    busyUntil = newBusy;
  }

  uint64_t nextId{0};
  sim::TimeUs busyUntil{0};
  std::unordered_map<uint64_t, AirTx> ongoing;
  std::vector<IAirListener*> listeners;

  LogDistancePropagation prop;
  double noiseW{DbmToW(-100.0)};
};

} // namespace wifi
