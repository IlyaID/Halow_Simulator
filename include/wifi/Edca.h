#pragma once
#include "../sim/Simulator.h"
#include "../sim/Rng.h"
#include "../sim/Time.h"
#include "Types.h"
#include "Packet.h"
#include "Raw.h"
#include <deque>
#include <array>
#include <optional>
#include <memory>

namespace wifi {

struct EdcaParams {
  int aifsn{3};
  int cwMin{15};
  int cwMax{1023};
  int retryLimit{7};
  sim::TimeUs txopLimitUs{sim::US(0)}; // 0 => один обмен
};

struct MacTiming {
  sim::TimeUs slotTimeUs{sim::US(52)};
  sim::TimeUs sifsUs{sim::US(160)};
  sim::TimeUs aifsUs(Ac ac, const std::array<EdcaParams,4>& p) const {
    // AIFS = SIFS + AIFSN * SlotTime
    return sifsUs + sim::TimeUs(p[int(ac)].aifsn) * slotTimeUs;
  }
};

class Node; // fwd

class EdcaAc {
public:
  EdcaAc(Node* owner, Ac ac, sim::Rng* rng);

  std::deque<Packet*>& AccessQueue() { return q; }

  void Configure(const EdcaParams& p);
  void Enqueue(Packet* p);

  bool HasPacket() const { return !q.empty(); }
  Ac GetAc() const { return m_ac; }

  void Pause(sim::TimeUs now);
  void Resume(sim::TimeUs now);

  void OnTxSuccess();
  void OnTxFail();

  bool IsReady(sim::TimeUs /*now*/) const {
    return active && aifsRemainingUs == 0 && backoffSlots == 0;
  }

  Packet* Peek() const { return q.empty() ? nullptr : q.front(); }
  Packet* Pop() {
    if (q.empty()) return nullptr;
    auto* p = q.front();
    q.pop_front();
    return p;
  }

  void ResetContention();
  void StartOrResume();
  void OnExpiry();

  bool active{false};

private:
  void ScheduleExpiry(sim::TimeUs now);
  void UpdateProgress(sim::TimeUs now);

  Node* m_owner;
  Ac m_ac;
  sim::Rng* m_rng;

  std::deque<Packet*> q;

  EdcaParams params{};
  int cw{15};
  int retry{0};

  sim::TimeUs aifsRemainingUs{0};
  int backoffSlots{0};
  sim::TimeUs slotRemainderUs{0};

  sim::TimeUs lastUpdateUs{0};
  std::optional<sim::EventId> expiryEvent;
};


} // namespace wifi

