#pragma once
#include "Types.h"
#include "Packet.h"
#include "Propagation.h"
#include "Medium.h"
#include "../sim/Rng.h"
#include "../sim/Simulator.h"
#include <optional>
#include <vector>
#include <cmath>

namespace wifi {

enum class PhyMode : int { IDEAL=0, SNR_PER=1, CAPTURE=2 };
enum class RxFailReason : int { NONE=0, COLLISION=1, PER=2, NOT_FOR_ME=3 };

struct PhyConfig {
  PhyMode mode{PhyMode::SNR_PER};
  double txPowerDbm{0.0};

  // fixed MCS (rate comes from McsDb elsewhere; here just store selection)
  int mcs{0};
  int cbwMhz{1};
  double giUs{0.0};
  int nss{1};

  // airtime model (simplified)
  sim::TimeUs preambleUs{200};
  double fallbackRateBps{600000.0}; // if CSV doesn't have the entry

  // capture + CCA + noise
  double captureThresholdDb{6.0};
  double ccaEdThresholdDbm{-82.0};
  double noiseDbm{-100.0};

  // MAC timing anchor (SIFS used later by MAC)
  sim::TimeUs sifsUs{160};
};

struct RxResult {
  bool ok{false};
  RxFailReason fail{RxFailReason::NONE};
  uint64_t txId{0};
  NodeId tx{-1};
  NodeId rx{-1};
  Frame frame{};
  double rssiDbm{-999.0};
  double sinr{0.0};
};

class IPhyRxSink {
public:
  virtual ~IPhyRxSink() = default;
  virtual NodeId Id() const = 0;
  virtual Vec2 Pos() const = 0;
  virtual void OnPhyRxResult(const RxResult& r) = 0;
};

struct RxAttempt {
  uint64_t txId{0};
  NodeId txNode{-1};
  Frame frame{};
  sim::TimeUs tStart{0};
  sim::TimeUs tEnd{0};
  double rxPowerW{0.0};
};

class Phy : public IAirListener {
public:
  Phy(IPhyRxSink* sink, Medium* m, sim::Rng* rng, PhyConfig cfg)
    : s(sink), medium(m), R(rng), c(cfg) {}

  const PhyConfig& Cfg() const { return c; }

  // IAirListener
  NodeId ListenerId() const override { return s ? s->Id() : -1; }
  Vec2   ListenerPos() const override { return s ? s->Pos() : Vec2{}; }

  double NoiseW() const { return DbmToW(c.noiseDbm); }

  bool CcaBusy(sim::TimeUs /*now*/, double sumW) const {
    return WToDbm(sumW) >= c.ccaEdThresholdDbm;
  }

  sim::TimeUs TxDurationUs(int bytes, double rateBps) const {
    double t = double(bytes * 8) / rateBps;
    return c.preambleUs + sim::TimeUs(t * 1e6);
  }

  void OnAirTxStart(const AirTx& tx, double rxPowerW) override {
    if (!s) return;
    if (tx.txNode == s->Id()) return;

    RxAttempt a;
    a.txId = tx.txId;
    a.txNode = tx.txNode;
    a.frame = tx.frame;
    a.tStart = tx.tStart;
    a.tEnd = tx.tEnd;
    a.rxPowerW = rxPowerW;

    if (!rx.has_value()) {
      rx = a;
      return;
    }

    // capture: may resync to stronger incoming packet
    if (c.mode == PhyMode::CAPTURE) {
      double curW = rx->rxPowerW;
      double neuW = a.rxPowerW;
      double ratioDb = 10.0 * std::log10(neuW / curW);
      if (ratioDb >= c.captureThresholdDb) {
        overlaps.push_back(*rx);
        rx = a;
        return;
      }
    }

    overlaps.push_back(a);
  }

  void OnAirTxEnd(const AirTx& tx, double /*rxPowerW*/) override {
    if (!s || !rx.has_value()) return;
    if (rx->txId != tx.txId) return;

    RxResult rr;
    rr.txId = rx->txId;
    rr.tx = rx->txNode;
    rr.rx = s->Id();
    rr.frame = rx->frame;
    rr.rssiDbm = WToDbm(rx->rxPowerW);

    bool forMe = (rr.frame.dst == s->Id()) || (rr.frame.dst == BROADCAST);
    if (!forMe) {
      rr.ok = false;
      rr.fail = RxFailReason::NOT_FOR_ME;
      s->OnPhyRxResult(rr);
      ClearRx();
      return;
    }

    // interference from overlaps + noise (simple sum)
    double signalW = rx->rxPowerW;
    double interfW = NoiseW();
    for (auto& o : overlaps) {
      if (o.tStart < rx->tEnd && o.tEnd > rx->tStart) interfW += o.rxPowerW;
    }
    rr.sinr = (interfW > 0) ? (signalW / interfW) : 0.0;

    bool ok = true;
    if (c.mode == PhyMode::IDEAL) {
      ok = overlaps.empty();  // pure collision model
      rr.fail = ok ? RxFailReason::NONE : RxFailReason::COLLISION;
    } else {
      ok = DecodeByPer(rr.sinr, rx->frame.bytes);
      rr.fail = ok ? RxFailReason::NONE : RxFailReason::PER;
    }

    rr.ok = ok;
    s->OnPhyRxResult(rr);
    ClearRx();
  }

private:
  bool DecodeByPer(double sinr, int bytes) {
    if (sinr <= 0) return false;

    // Placeholder BER->PER (later we can swap per-MCS curves)
    double ber = 0.5 * std::erfc(std::sqrt(sinr));
    int bits = bytes * 8;
    double per = 1.0 - std::pow(1.0 - ber, bits);
    return !R->Bernoulli(per);
  }

  void ClearRx() {
    rx.reset();
    overlaps.clear();
  }

  IPhyRxSink* s{nullptr};
  Medium* medium{nullptr};
  sim::Rng* R{nullptr};
  PhyConfig c{};

  std::optional<RxAttempt> rx;
  std::vector<RxAttempt> overlaps;
};

} // namespace wifi
