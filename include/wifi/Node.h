#pragma once
#include "Types.h"
#include "Packet.h"
#include "Phy.h"
#include "Medium.h"
#include "Raw.h"
#include "Edca.h"
#include "Energy.h"
#include "Stats.h"
#include "BlockAck.h"
#include "../sim/Rng.h"
#include "../sim/Simulator.h"
#include <array>
#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace wifi {

struct NodeConfig {
  bool isAp{false};
  NodeId id{0};
  Vec2 pos{};
};

// глобальный реестр узлов, определение — в main.cc
extern std::vector<class Node*> g_nodes;

class Node : public IPhyRxSink {
public:
  Node(NodeConfig nc,
       Medium* m,
       sim::Rng* rng,
       PhyConfig pcfg,
       RawConfig rcfg,
       MacTiming timing_,
       std::array<EdcaParams,4> edca,
       Stats* stats_)
    : cfg(nc),
      medium(m),
      R(rng),
      phy(this, m, rng, pcfg),
      rawGate(nc.id, rcfg),
      timing(timing_),
      stats(stats_) {

    edcaParams = edca;
    for (int i = 0; i < 4; ++i) {
      acs[i] = std::make_unique<EdcaAc>(this, Ac(i), R);
      acs[i]->Configure(edcaParams[i]);
    }
    energy.SetState(RadioState::IDLE);
  }

  // IPhyRxSink
  NodeId Id() const override { return cfg.id; }
  Vec2 Pos() const override { return cfg.pos; }

  void OnPhyRxResult(const RxResult& r) override {
    auto now = sim::Simulator::Instance().Now();
    if (!r.ok) {
      if (r.fail == RxFailReason::COLLISION) {
        energy.SetState(RadioState::COLLISION);
      } else {
        energy.SetState(RadioState::IDLE);
      }
      return;
    }

    energy.SetState(RadioState::RX);

    if (r.frame.type == FrameType::DATA) {
      if (cfg.isAp) {
        // AP принимает uplink DATA / A-MPDU
        if (!r.frame.mpdus.empty()) {
          // seq из UL A-MPDU
          std::vector<uint16_t> seqs;
          seqs.reserve(r.frame.mpdus.size());
          for (auto& md : r.frame.mpdus) {
            if (!md.pkt) continue;
            seqs.push_back(md.seq);
            md.pkt->tRxDone = now;
            md.pkt->success = true;
            if (stats) stats->RecordPacket(*md.pkt);
            // память UL пакета освобождает STA после BA
          }
          if (!seqs.empty()) {
            uint16_t ssn = *std::min_element(seqs.begin(), seqs.end());
            uint16_t maxSeq = *std::max_element(seqs.begin(), seqs.end());
            size_t win = size_t(maxSeq - ssn + 1);
            std::vector<bool> bitmap(win, false);
            for (auto s : seqs) {
              bitmap[size_t(s - ssn)] = true;
            }
            // отправитель STA
            NodeId staId = r.tx;
            if (staId >= 0 && staId < (NodeId)g_nodes.size()
                && g_nodes[staId]) {
              g_nodes[staId]->OnUlBlockAckFromAp(ssn, bitmap);
            }
          }
        } else if (r.frame.pkt) {
          // одиночный UL MPDU
          r.frame.pkt->tRxDone = now;
          r.frame.pkt->success = true;
          if (stats) stats->RecordPacket(*r.frame.pkt);
          // освобождение UL пакета — на STA (владельце)
        }
      } else {
        // STA принимает downlink DATA
        if (!r.frame.mpdus.empty()) {
          for (auto& md : r.frame.mpdus) {
            if (!md.pkt) continue;
            md.pkt->tRxDone = now;
            md.pkt->success = true;
            if (stats) stats->RecordPacket(*md.pkt);
            // DL память освободит AP через Pop()/delete
          }
        } else if (r.frame.pkt) {
          r.frame.pkt->tRxDone = now;
          r.frame.pkt->success = true;
          if (stats) stats->RecordPacket(*r.frame.pkt);
          // DL одиночный: delete делает AP
        }
      }
    }

    energy.SetState(RadioState::IDLE);
  }

  // Medium callbacks
  void OnMediumBusy() {
    auto now = sim::Simulator::Instance().Now();
    for (auto& a : acs) a->Pause(now);
  }

  void OnMediumIdle() {
    auto now = sim::Simulator::Instance().Now();
    for (auto& a : acs) a->Resume(now);
  }

  // Вход из TrafficGenerator
  void Enqueue(Packet* p) {
    acs[int(p->ac)]->Enqueue(p);
  }

  // Сигнал от EdcaAc, что AIFS+backoff дошли до нуля
  void OnAcExpiry(Ac /*ac*/) {
    auto now = sim::Simulator::Instance().Now();
    int best = -1;
    for (int i = 0; i < 4; ++i) {
      if (acs[i]->IsReady(now) && acs[i]->HasPacket()) best = i;
    }
    if (best < 0) return;

    for (int i = 0; i < 4; ++i) {
      if (i != best && acs[i]->IsReady(now) && acs[i]->HasPacket()) {
        acs[i]->OnTxFail();
      }
    }

    AttemptTx(Ac(best));
  }

  // Обработка BlockAck от AP (для STA)
  void OnUlBlockAckFromAp(uint16_t ssn, const std::vector<bool>& bitmap) {
    if (cfg.isAp) return;

    // Обновляем BA-сессию: она возвращает seq, требующие ретранса
    auto needRetrans = ulBaSession.OnBlockAck(ssn, bitmap);

    // 1) Подтверждённые seq (bitmap=true) — удаляем из ulOutstanding и освобождаем память
    for (size_t i = 0; i < bitmap.size(); ++i) {
      if (!bitmap[i]) continue;
      uint16_t seq = uint16_t(ssn + i);
      auto it = ulOutstanding.find(seq);
      if (it == ulOutstanding.end()) continue;
      Packet* p = it->second;
      ulOutstanding.erase(it);
      if (p) {
        delete p;            // единственное место delete для успешных UL пакетов
      }
    }

    // 2) Пакеты из needRetrans остаются в очереди и в ulOutstanding.
    auto& acRef = *acs[int(Ac::BE)]; // предполагаем UL только в BE
    if (!needRetrans.empty()) {
      acRef.OnTxFail();    // даст ретрансмиссию
    } else {
      acRef.OnTxSuccess(); // сброс contention window
    }
  }

  void AttemptTx(Ac ac) {
    auto now = sim::Simulator::Instance().Now();
    if (medium->IsBusy(now)) {
      acs[int(ac)]->Pause(now);
      return;
    }

    if (!cfg.isAp) {
      // STA uplink: A-MPDU с несколькими MPDU и BA
      NodeId ra = 0; // один AP с id=0
      sim::TimeUs budget = edcaParams[int(ac)].txopLimitUs;
      if (budget <= 0) budget = sim::US(10000);

      auto& acRef = *acs[int(ac)];
      auto& q = acRef.AccessQueue();
      if (q.empty()) {
        acRef.ResetContention();
        return;
      }

      const int maxMpdu  = 16;
      const int maxBytes = 4096;

      std::vector<MpduDesc> mpdus;
      mpdus.reserve(maxMpdu);

      int totalBytes = 0;
      sim::TimeUs usedDataUs{0};

      for (auto it = q.begin(); it != q.end(); ++it) {
        Packet* p = *it;
        if (!p) continue;
        if (p->dst != ra) continue; // агрегируем только к AP

        int mpduBytes = p->payloadBytes + macOverheadBytes;
        int newTotal  = totalBytes + mpduBytes;

        double t = double(mpduBytes * 8) / TxRateBps();
        sim::TimeUs dUs = sim::TimeUs(t * 1e6);

        if ((int)mpdus.size() >= maxMpdu) break;
        if (newTotal > maxBytes)       break;
        if (usedDataUs + dUs > budget) break;

        // Назначаем seq и регистрируем в BA-сессии и карте outstanding
        p->seq = nextUlSeq++;
        ulBaSession.OnMpduQueued(p->seq);
        ulOutstanding[p->seq] = p;

        MpduDesc md{};
        md.pkt = p;
        md.seq = p->seq;
        mpdus.push_back(md);

        totalBytes = newTotal;
        usedDataUs += dUs;
      }

      if (mpdus.empty()) {
        acRef.ResetContention();
        return;
      }

      // RAW gate по всей A-MPDU
      sim::TimeUs exchangeUs = usedDataUs + phy.Cfg().sifsUs;
      auto now2 = now;
      if (!rawGate.CanStartTx(now2, exchangeUs)) {
        acRef.Pause(now2);
        energy.SetState(RadioState::SLEEP);
        sim::TimeUs wake = rawGate.c.enabled
          ? rawGate.SlotEnd(now2)
          : (now2 + sim::US(1000));
        sim::Simulator::Instance().Schedule(wake, [this]{
          auto t2 = sim::Simulator::Instance().Now();
          for (auto& a : acs) a->Resume(t2);
          energy.SetState(RadioState::IDLE);
        });
        return;
      }

      Frame f{};
      f.type  = FrameType::DATA;
      f.src   = Id();
      f.dst   = ra;
      f.ac    = ac;
      f.mpdus = std::move(mpdus);
      f.bytes = totalBytes;

      for (auto& md : f.mpdus) {
        if (md.pkt) md.pkt->tTxStart = now;
      }

      energy.SetState(RadioState::TX);
      sim::TimeUs dur = phy.TxDurationUs(f.bytes, TxRateBps());
      medium->StartTx(Id(), f, dur, phy.Cfg().txPowerDbm);

      // Завершение TXOP, успех/фейл придёт через BA от AP
      sim::Simulator::Instance().Schedule(now + dur, [this]{
        energy.SetState(RadioState::IDLE);
      });
      return;
    }

    // AP downlink: одиночный MPDU (как прежде)
    auto* p = acs[int(ac)]->Peek();
    if (!p) {
      acs[int(ac)]->ResetContention();
      return;
    }

    Frame f{};
    f.type  = FrameType::DATA;
    f.pkt   = p;
    f.src   = Id();
    f.dst   = p->dst;
    f.ac    = ac;
    f.bytes = p->payloadBytes + macOverheadBytes;

    p->tTxStart = now;
    energy.SetState(RadioState::TX);

    sim::TimeUs dur = phy.TxDurationUs(f.bytes, TxRateBps());
    medium->StartTx(Id(), f, dur, phy.Cfg().txPowerDbm);

    sim::Simulator::Instance().Schedule(now + dur, [this, ac]{
      // пока DL без честного ACK/BA — считаем, что TX не подтверждён
      acs[int(ac)]->OnTxFail();
      energy.SetState(RadioState::IDLE);
    });
  }

  double TxRateBps() const { return txRateBps; }
  void SetTxRateBps(double r) { txRateBps = r; }

  double EnergyJ() { return energy.Joules(); }

  NodeConfig cfg;
  Medium*   medium{nullptr};
  sim::Rng* R{nullptr};
  Phy       phy;
  RawGate   rawGate;
  MacTiming timing;
  EnergyModel energy;
  Stats*    stats{nullptr};

  static constexpr int macOverheadBytes = 30;

  std::array<std::unique_ptr<EdcaAc>,4> acs;
  std::array<EdcaParams,4>               edcaParams{};

private:
  double txRateBps{600000.0};

  // UL BlockAck (STA -> AP)
  BlockAckSession ulBaSession{64};
  uint16_t nextUlSeq{0};
  std::unordered_map<uint16_t, Packet*> ulOutstanding;
};

} // namespace wifi
