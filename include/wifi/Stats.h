#pragma once
#include "Packet.h"
#include "../sim/Time.h"
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

namespace wifi {

struct Summary {
  double throughput_bps{0.0};
  double p50_us{0.0}, p90_us{0.0}, p99_us{0.0};
  double energy_j{0.0};
  double bits_per_joule{0.0};
  uint64_t delivered_pkts{0};
};

class Stats {
public:
  void RecordPacket(const Packet& p) { packets.push_back(p); }

  static double Percentile(std::vector<double>& v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double pos = q * (v.size() - 1);
    size_t i = static_cast<size_t>(pos);
    size_t j = std::min(i + 1, v.size() - 1);
    double frac = pos - i;
    return v[i] * (1.0 - frac) + v[j] * frac;
  }

  void WriteResultsCsv(const std::string& path) const {
    std::ofstream f(path);
    f << "id,src,dst,ac,payloadBytes,tCreated_us,tEnqueued_us,tTxStart_us,tRxDone_us,delay_us,success\n";
    for (auto& p : packets) {
      auto delay = (p.success ? (p.tRxDone - p.tCreated) : 0);
      f << p.id << "," << p.src << "," << p.dst << "," << static_cast<int>(p.ac) << ","
        << p.payloadBytes << ","
        << p.tCreated << "," << p.tEnqueued << "," << p.tTxStart << "," << p.tRxDone << ","
        << delay << "," << (p.success ? 1 : 0) << "\n";
    }
  }

  Summary ComputeSummary(sim::TimeUs simTimeUs, double totalEnergyJ) const {
    Summary s{};
    uint64_t bits = 0;
    std::vector<double> delays;
    for (auto& p : packets) {
      if (!p.success) continue;
      s.delivered_pkts++;
      bits += uint64_t(p.payloadBytes) * 8;
      delays.push_back(double(p.tRxDone - p.tCreated));
    }
    s.throughput_bps = (simTimeUs > 0) ? (double(bits) / (double(simTimeUs) * 1e-6)) : 0.0;
    s.p50_us = Percentile(delays, 0.50);
    s.p90_us = Percentile(delays, 0.90);
    s.p99_us = Percentile(delays, 0.99);
    s.energy_j = totalEnergyJ;
    s.bits_per_joule = (totalEnergyJ > 0) ? (double(bits) / totalEnergyJ) : 0.0;
    return s;
  }

  void WriteSummaryCsv(const std::string& path, const Summary& s) const {
    std::ofstream f(path);
    f << "throughput_bps,delivered_pkts,p50_us,p90_us,p99_us,energy_j,bits_per_joule\n";
    f << s.throughput_bps << "," << s.delivered_pkts << ","
      << s.p50_us << "," << s.p90_us << "," << s.p99_us << ","
      << s.energy_j << "," << s.bits_per_joule << "\n";
  }

  std::vector<Packet> packets;
};

} // namespace wifi
