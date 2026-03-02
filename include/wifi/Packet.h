#pragma once
#include "Types.h"
#include "../sim/Time.h"
#include <cstdint>
#include <vector>

namespace wifi {

struct Packet {
  uint64_t id{0};
  NodeId src{-1};
  NodeId dst{-1};
  Ac ac{Ac::BE};
  int payloadBytes{0};

  // Для BlockAck / упорядочивания
  uint16_t seq{0};

  sim::TimeUs tCreated{0};
  sim::TimeUs tEnqueued{0};
  sim::TimeUs tTxStart{0};
  sim::TimeUs tRxDone{0};
  bool success{false};
};

// Один MPDU в A‑MPDU (для BA/ретраев)
struct MpduDesc {
  Packet* pkt{nullptr};
  uint16_t seq{0};
};


// Frame может быть одиночным MPDU или A‑MPDU (вектор mpdus)
struct Frame {
  FrameType type{FrameType::DATA};

  // Для простых одиночных MPDU (старый путь)
  Packet* pkt{nullptr};

  // Для A‑MPDU (несколько MPDU в одном PPDU)
  std::vector<MpduDesc> mpdus;

  NodeId src{-1};
  NodeId dst{-1};
  Ac ac{Ac::BE};
  int bytes{0}; // полный размер PPDU (пойнт на airtime)
};

} // namespace wifi
