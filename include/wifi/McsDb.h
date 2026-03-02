#pragma once
#include "../util/Csv.h"
#include "../util/Result.h"
#include <vector>
#include <string>

namespace wifi {

struct McsEntry {
  int mcs{0};
  int cbwMhz{1};
  double giUs{0.0};
  int nss{1};
  double rateBps{0.0};
};

class McsDb {
public:
  util::Result<void> LoadCsv(const std::string& path) {
    auto t = util::ReadCsv(path, true);
    if (!t.ok()) return util::Result<void>::Err(t.error());

    int c_mcs  = t.value().ColIndex("mcs");
    int c_cbw  = t.value().ColIndex("cbw_mhz");
    int c_gi   = t.value().ColIndex("gi_us");
    int c_nss  = t.value().ColIndex("nss");
    int c_rate = t.value().ColIndex("rate_bps");
    if (c_mcs < 0 || c_cbw < 0 || c_gi < 0 || c_nss < 0 || c_rate < 0) {
      return util::Result<void>::Err("MCS CSV missing required columns: mcs,cbw_mhz,gi_us,nss,rate_bps");
    }

    entries.clear();
    entries.reserve(t.value().rows.size());

    for (auto& r : t.value().rows) {
      if ((int)r.size() <= std::max({c_mcs, c_cbw, c_gi, c_nss, c_rate})) continue;
      McsEntry e;
      e.mcs = std::stoi(r[c_mcs]);
      e.cbwMhz = std::stoi(r[c_cbw]);
      e.giUs = std::stod(r[c_gi]);
      e.nss = std::stoi(r[c_nss]);
      e.rateBps = std::stod(r[c_rate]);
      entries.push_back(e);
    }

    return util::Result<void>::Ok();
  }

  const McsEntry* Find(int mcs, int cbwMhz, double giUs, int nss) const {
    for (auto& e : entries) {
      if (e.mcs == mcs && e.cbwMhz == cbwMhz && e.giUs == giUs && e.nss == nss) return &e;
    }
    return nullptr;
  }

  double RateBpsOr(double fallback, int mcs, int cbwMhz, double giUs, int nss) const {
    if (auto* e = Find(mcs, cbwMhz, giUs, nss)) return e->rateBps;
    return fallback;
  }

  std::vector<McsEntry> entries;
};

} // namespace wifi
