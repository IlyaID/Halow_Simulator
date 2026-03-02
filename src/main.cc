#include "../include/sim/Simulator.h"
#include "../include/sim/Rng.h"
#include "../include/wifi/McsDb.h"
#include "../include/wifi/Medium.h"
#include "../include/wifi/Phy.h"
#include "../include/wifi/Node.h"
#include "../include/wifi/Raw.h"
#include "../include/wifi/Edca.h"
#include "../include/wifi/Stats.h"
#include "../include/wifi/Traffic.h"
#include "../include/util/Json.h"
#include <iostream>
#include <vector>
#include <memory>
#include <cmath>
#include <unordered_map>

using namespace sim;
using namespace wifi;

// Глобальный реестр узлов, используется для BA callback
namespace wifi {
  std::vector<Node*> g_nodes;
}

struct ScenarioCfg {
  int nSta{40};
  TimeUs simTime{S(20)};
  PhyMode phyMode{PhyMode::CAPTURE};

  double areaRadius{50.0};
  Vec2 apPos{0.0, 0.0};

  RawConfig raw{};

  // traffic
  double ulLambda{0.2};
  double dlLambda{0.2};
  int payloadBytes{64};
};

static Vec2 PlaceOnCircle(int i, int n, double r) {
  double ang = 2.0 * M_PI * (double(i) / double(n));
  return Vec2{r * std::cos(ang), r * std::sin(ang)};
}

ScenarioCfg LoadScenario(const std::string& path) {
  ScenarioCfg sc{};
  auto jr = util::ReadJson(path);
  if (!jr.ok()) {
    std::cerr << "Warning: cannot load scenario JSON: " << jr.error()
              << " (using defaults)\n";
    return sc;
  }
  const util::Json& j = jr.value();

  sc.nSta = j.value<int>("nSta", sc.nSta);
  double simTime_s = j.value<double>("simTime_s", 20.0);
  sc.simTime = S(simTime_s);

  std::string phy = j.value<std::string>("phyMode", "CAPTURE");
  if (phy == "IDEAL") sc.phyMode = PhyMode::IDEAL;
  else if (phy == "SNR_PER") sc.phyMode = PhyMode::SNR_PER;
  else sc.phyMode = PhyMode::CAPTURE;

  sc.areaRadius = j.value<double>("areaRadius", sc.areaRadius);
  auto apPos = j.valueArrNum("apPos", {0.0, 0.0});
  if (apPos.size() >= 2) sc.apPos = Vec2{apPos[0], apPos[1]};

  if (const util::Json* rj = j.getPtr("raw")) {
    sc.raw.enabled = rj->value<bool>("enabled", true);
    double bi_ms = rj->value<double>("beaconInterval_ms", 1024.0);
    sc.raw.beaconIntervalUs = MS(bi_ms);
    double rawOff_us = rj->value<double>("rawStartOffset_us", 2000.0);
    sc.raw.rawStartOffsetUs = US(rawOff_us);
    double slot_us = rj->value<double>("slot_us", 20000.0);
    sc.raw.slotUs = US(slot_us);
    sc.raw.numSlots = rj->value<int>("numSlots", 8);
    sc.raw.csbEnabled = rj->value<bool>("csbEnabled", true);
  }

  if (const util::Json* tj = j.getPtr("traffic")) {
    sc.ulLambda = tj->value<double>("ul_lambda_per_sec", sc.ulLambda);
    sc.dlLambda = tj->value<double>("dl_lambda_per_sec", sc.dlLambda);
    sc.payloadBytes = tj->value<int>("payloadBytes", sc.payloadBytes);
  }

  return sc;
}

int main() {
  Simulator::Instance().Reset();
  Rng rng(12345);

  ScenarioCfg sc = LoadScenario("../scenario.json");

  // MCS table (optional)
  McsDb db;
  auto mcsRes = db.LoadCsv("configs/phy_s1g_mcs.csv");
  if (!mcsRes.ok()) {
    std::cerr << "Warning: cannot load MCS CSV: " << mcsRes.error()
              << " (using fallbackRateBps)\n";
  }

  // Medium + propagation
  LogDistancePropagation prop;
  prop.pl0_db = 40.0;
  prop.n = 3.0;
  Medium medium(prop);
  medium.SetNoiseDbm(-100.0);

  Stats stats;

  // PHY config
  PhyConfig pcfg;
  pcfg.mode = sc.phyMode;
  pcfg.txPowerDbm = 0.0;
  pcfg.captureThresholdDb = 6.0;
  pcfg.noiseDbm = -100.0;
  pcfg.mcs = 2;
  pcfg.cbwMhz = 1;
  pcfg.giUs = 0.0;
  pcfg.nss = 1;
  pcfg.fallbackRateBps = 600000.0;
  pcfg.fallbackRateBps = db.RateBpsOr(pcfg.fallbackRateBps,
                                      pcfg.mcs, pcfg.cbwMhz, pcfg.giUs, pcfg.nss);
  pcfg.preambleUs = 200;
  pcfg.sifsUs = 160;

  // EDCA defaults
  std::array<EdcaParams,4> edca{};
  edca[int(Ac::BK)] = EdcaParams{7, 15, 1023, 7, US(0)};
  edca[int(Ac::BE)] = EdcaParams{3, 15, 1023, 7, US(0)};
  edca[int(Ac::VI)] = EdcaParams{2, 7, 15, 7, US(3000)};
  edca[int(Ac::VO)] = EdcaParams{2, 3, 7, 7, US(3000)};

  MacTiming timing;
  timing.slotTimeUs = US(52);
  timing.sifsUs = US(160);

  int apId = 0;
  int total = 1 + sc.nSta;

  std::vector<std::unique_ptr<Node>> nodes;
  nodes.reserve(total);
  g_nodes.clear();
  g_nodes.resize(total, nullptr);

  // AP
  {
    NodeConfig nc;
    nc.isAp = true;
    nc.id = apId;
    nc.pos = sc.apPos;

    nodes.emplace_back(std::make_unique<Node>(nc, &medium, &rng, pcfg,
                                              RawConfig{}, timing, edca, &stats));
    g_nodes[nc.id] = nodes.back().get();
  }

  // STAs
  for (int i = 0; i < sc.nSta; ++i) {
    NodeConfig nc;
    nc.isAp = false;
    nc.id = i + 1;
    nc.pos = PlaceOnCircle(i, sc.nSta, sc.areaRadius);

    nodes.emplace_back(std::make_unique<Node>(nc, &medium, &rng, pcfg,
                                              sc.raw, timing, edca, &stats));
    g_nodes[nc.id] = nodes.back().get();
  }

  // Medium position callback
  medium.posOf = [&](NodeId id) -> Vec2 {
    if (id < 0 || id >= (NodeId)nodes.size()) return Vec2{};
    return nodes[(size_t)id]->Pos();
  };

  // Register PHYs as listeners
  for (auto& n : nodes) {
    medium.Register(&n->phy);
    n->SetTxRateBps(pcfg.fallbackRateBps);
  }

  // Traffic: UL (STA->AP) + DL (AP->STA) Poisson
  std::vector<std::unique_ptr<TrafficGenerator>> gens;
  gens.reserve(sc.nSta * 2);

  // UL
for (int i = 0; i < sc.nSta; ++i) {
  NodeId staId = i + 1;
  TrafficCfg tc;
  tc.poisson = true;
  tc.lambdaPerSec = sc.ulLambda;
  tc.payloadBytes = sc.payloadBytes;
  tc.ac = Ac::BE;

  Node* staNode = nodes[(size_t)staId].get();  // захватываем указатель

  gens.emplace_back(std::make_unique<TrafficGenerator>(
    staId, apId, &rng, tc,
    [staNode](Packet* p){ staNode->Enqueue(p); }   // только staNode по значению
  ));
  gens.back()->Start(US(1000 + i * 100));
}

// DL
for (int i = 0; i < sc.nSta; ++i) {
  NodeId staId = i + 1;
  TrafficCfg tc;
  tc.poisson = true;
  tc.lambdaPerSec = sc.dlLambda;
  tc.payloadBytes = sc.payloadBytes;
  tc.ac = Ac::BE;

  Node* apNode = nodes[(size_t)apId].get();

  gens.emplace_back(std::make_unique<TrafficGenerator>(
    apId, staId, &rng, tc,
    [apNode](Packet* p){ apNode->Enqueue(p); }
  ));
  gens.back()->Start(US(2000 + i * 100));
}


  // DL
  for (int i = 0; i < sc.nSta; ++i) {
    NodeId staId = i + 1;
    TrafficCfg tc;
    tc.poisson = true;
    tc.lambdaPerSec = sc.dlLambda;
    tc.payloadBytes = sc.payloadBytes;
    tc.ac = Ac::BE;

    gens.emplace_back(std::make_unique<TrafficGenerator>(
      apId, staId, &rng, tc,
      [&](Packet* p){ nodes[(size_t)apId]->Enqueue(p); }
    ));
    gens.back()->Start(US(2000 + i * 100));
  }

  // Initial idle to kick EDCA
  Simulator::Instance().Schedule(0, [&]{
    for (auto& n : nodes) n->OnMediumIdle();
  });

  Simulator::Instance().Run(sc.simTime);

  double totalEnergyJ = 0.0;
  for (auto& n : nodes) totalEnergyJ += n->EnergyJ();

  stats.WriteResultsCsv("../results.csv");
  auto sum = stats.ComputeSummary(sc.simTime, totalEnergyJ);
  stats.WriteSummaryCsv("../summary.csv", sum);

  // Дополнительные CSV: энергия по узлам
  {
    std::ofstream f("../energy_per_node.csv");
    f << "node_id,is_ap,energy_j\n";
    for (auto& n : nodes) {
      f << n->cfg.id << ","
        << (n->cfg.isAp ? 1 : 0) << ","
        << n->EnergyJ() << "\n";
    }
  }

  // Простая агрегация по исходнику (сколько бит передал каждый src)
  {
    std::unordered_map<NodeId, uint64_t> bitsPerSrc;
    for (auto& p : stats.packets) {
      if (!p.success) continue;
      bitsPerSrc[p.src] += uint64_t(p.payloadBytes) * 8;
    }
    std::ofstream f("../throughput_per_node.csv");
    f << "node_id,bits\n";
    for (auto& kv : bitsPerSrc) {
      f << kv.first << "," << kv.second << "\n";
    }
  }

  std::cout << "Done.\n"
            << "Throughput (bps): " << sum.throughput_bps << "\n"
            << "Delivered: " << sum.delivered_pkts << "\n"
            << "Delay p50/p90/p99 (us): " << sum.p50_us << " / "
                                       << sum.p90_us << " / "
                                       << sum.p99_us << "\n"
            << "Energy (J): " << sum.energy_j << "\n"
            << "Bits/J: " << sum.bits_per_joule << "\n";

  return 0;
}