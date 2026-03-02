#pragma once
#include "../sim/Simulator.h"
#include "../sim/Rng.h"
#include "../sim/Time.h"
#include "Packet.h"
#include "Types.h"
#include <functional>

namespace wifi {

struct TrafficCfg {
  bool poisson{true};
  double lambdaPerSec{0.2};
  sim::TimeUs periodicUs{sim::S(5)};
  int payloadBytes{64};
  Ac ac{Ac::BE};
};

class TrafficGenerator {
public:
  using EnqueueFn = std::function<void(Packet*)>;

  TrafficGenerator(NodeId src, NodeId dst, sim::Rng* rng, TrafficCfg cfg, EnqueueFn enq)
    : s(src), d(dst), R(rng), c(cfg), enqueue(std::move(enq)) {}

  void Start(sim::TimeUs t0) {
    sim::Simulator::Instance().Schedule(t0, [this]{ Generate(); });
  }

private:
  void Generate() {
    auto now = sim::Simulator::Instance().Now();
    auto* p = new Packet{};
    p->id = ++nextId;
    p->src = s; p->dst = d;
    p->ac = c.ac;
    p->payloadBytes = c.payloadBytes;
    p->tCreated = now;
    enqueue(p);

    sim::TimeUs dt = 0;
    if (c.poisson) {
      double x = R->Exp(c.lambdaPerSec);
      dt = sim::TimeUs(x * 1e6);
      if (dt < 1) dt = 1;
    } else {
      dt = c.periodicUs;
    }
    sim::Simulator::Instance().ScheduleIn(dt, [this]{ Generate(); });
  }

  NodeId s, d;
  sim::Rng* R;
  TrafficCfg c;
  EnqueueFn enqueue;
  uint64_t nextId{0};
};

} // namespace wifi
