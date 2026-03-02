#include "../include/wifi/Edca.h"
#include "../include/wifi/Node.h"  // полный Node

namespace wifi {

EdcaAc::EdcaAc(Node* owner, Ac ac, sim::Rng* rng)
  : m_owner(owner), m_ac(ac), m_rng(rng) {}

void EdcaAc::Configure(const EdcaParams& p) {
  params = p;
  ResetContention();
}

void EdcaAc::ResetContention() {
  cw = params.cwMin;
  retry = 0;
  active = false;
  aifsRemainingUs = 0;
  backoffSlots = 0;
  slotRemainderUs = 0;
  lastUpdateUs = 0;
  if (expiryEvent) sim::Simulator::Instance().Cancel(*expiryEvent);
  expiryEvent.reset();
}

void EdcaAc::Enqueue(Packet* p) {
  p->tEnqueued = sim::Simulator::Instance().Now();
  q.push_back(p);
  if (!active) StartOrResume();
}

void EdcaAc::UpdateProgress(sim::TimeUs now) {
  if (!active) { lastUpdateUs = now; return; }
  if (now <= lastUpdateUs) return;

  sim::TimeUs dt = now - lastUpdateUs;

  sim::TimeUs useAifs = std::min(dt, aifsRemainingUs);
  aifsRemainingUs -= useAifs;
  dt -= useAifs;

  if (aifsRemainingUs > 0) { lastUpdateUs = now; return; }

  auto slotUs = m_owner->timing.slotTimeUs;
  if (slotUs <= 0) slotUs = sim::US(1);

  slotRemainderUs += dt;
  int slots = int(slotRemainderUs / slotUs);
  if (slots > 0) {
    int dec = std::min(slots, backoffSlots);
    backoffSlots -= dec;
    slotRemainderUs -= sim::TimeUs(slots) * slotUs;
  }
  lastUpdateUs = now;
}

void EdcaAc::ScheduleExpiry(sim::TimeUs now) {
  if (expiryEvent) sim::Simulator::Instance().Cancel(*expiryEvent);
  auto slotUs = m_owner->timing.slotTimeUs;
  if (slotUs <= 0) slotUs = sim::US(1);

  sim::TimeUs t = aifsRemainingUs;
  if (aifsRemainingUs == 0) {
    sim::TimeUs slotsUs = sim::TimeUs(backoffSlots) * slotUs;
    t += std::max(sim::TimeUs(0), slotsUs - slotRemainderUs);
  }
  expiryEvent = sim::Simulator::Instance().Schedule(now + t, [this]{ this->OnExpiry(); });
}

void EdcaAc::StartOrResume() {
  auto now = sim::Simulator::Instance().Now();
  if (q.empty()) return;
  if (!m_owner->rawGate.CanContend(now)) { active = false; return; }

  if (!active) {
    active = true;
    cw = std::max(params.cwMin, std::min(cw, params.cwMax));
    aifsRemainingUs = m_owner->timing.aifsUs(m_ac, m_owner->edcaParams);
    backoffSlots = m_rng->UniformInt(0, cw);
    slotRemainderUs = 0;
    lastUpdateUs = now;
  } else {
    UpdateProgress(now);
  }

  if (m_owner->medium->IsBusy(now)) return;
  ScheduleExpiry(now);
}

void EdcaAc::Pause(sim::TimeUs now) {
  if (!active) return;
  UpdateProgress(now);
  if (expiryEvent) sim::Simulator::Instance().Cancel(*expiryEvent);
  expiryEvent.reset();
}

void EdcaAc::Resume(sim::TimeUs now) {
  if (q.empty()) { ResetContention(); return; }
  if (!m_owner->rawGate.CanContend(now)) return;
  if (!active) { StartOrResume(); return; }
  lastUpdateUs = now;
  if (!m_owner->medium->IsBusy(now)) ScheduleExpiry(now);
}

void EdcaAc::OnExpiry() {
  auto now = sim::Simulator::Instance().Now();
  UpdateProgress(now);
  if (q.empty()) { ResetContention(); return; }

  if (!m_owner->rawGate.CanContend(now)) { active = false; return; }
  if (m_owner->medium->IsBusy(now)) { Pause(now); return; }

  if (aifsRemainingUs == 0 && backoffSlots == 0) {
    m_owner->OnAcExpiry(m_ac);
  } else {
    ScheduleExpiry(now);
  }
}

void EdcaAc::OnTxSuccess() {
  cw = params.cwMin;
  retry = 0;
  if (q.empty()) { ResetContention(); return; }
  active = false;
  StartOrResume();
}

void EdcaAc::OnTxFail() {
  retry++;
  cw = std::min(params.cwMax, 2 * (cw + 1) - 1);
  if (retry > params.retryLimit) {
    auto* p = Pop();
    if (p) {
      p->success = false;
      if (m_owner->stats) m_owner->stats->RecordPacket(*p);
      delete p;
    }
    cw = params.cwMin;
    retry = 0;
  }
  active = false;
  StartOrResume();
}

} // namespace wifi
