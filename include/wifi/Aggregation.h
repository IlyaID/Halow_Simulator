#pragma once
#include "Packet.h"
#include "Edca.h"
#include "../sim/Time.h"
#include <vector>

namespace wifi {

// Упрощенный агрегатор A‑MPDU для UL (STA->AP).
class AmpduAggregator {
public:
  // maxMpdu: максимум MPDU в агрегате, maxBytes: защитный предел по размеру.
  static std::vector<MpduDesc> BuildAmpdu(EdcaAc& ac,
                                          NodeId ra,
                                          sim::TimeUs budgetUs,
                                          double rateBps,
                                          int macOverheadBytes,
                                          int maxMpdu = 16,
                                          int maxBytes = 4096) {
    std::vector<MpduDesc> out;
    if (!ac.HasPacket()) return out;

    auto now = sim::Simulator::Instance().Now();
    int totalBytes = 0;
    int count = 0;
    sim::TimeUs usedUs{0};

    std::deque<Packet*>& q = ac.AccessQueue(); // нужен accessor в EdcaAc

    for (auto it = q.begin(); it != q.end(); ++it) {
      Packet* p = *it;
      if (p->dst != ra) continue; // агрегируем только к одному RA

      int mpduBytes = p->payloadBytes + macOverheadBytes;
      int newTotal = totalBytes + mpduBytes;
      double t = double(mpduBytes * 8) / rateBps;
      sim::TimeUs dUs = sim::TimeUs(t * 1e6);

      if (count >= maxMpdu) break;
      if (newTotal > maxBytes) break;
      if (usedUs + dUs > budgetUs && budgetUs > 0) break;

      MpduDesc md;
      md.pkt = p;
      md.seq = nextSeq++;
      out.push_back(md);

      totalBytes = newTotal;
      usedUs += dUs;
      ++count;
    }

    return out;
  }

  static uint16_t nextSeq;
};

inline uint16_t AmpduAggregator::nextSeq = 0;

} // namespace wifi
