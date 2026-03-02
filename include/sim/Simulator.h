#pragma once
#include "Time.h"
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace sim {

using EventId = uint64_t;

struct Event {
  TimeUs t;
  EventId id;
  std::function<void()> fn;
  bool cancelled{false};
};

struct EventCmp {
  bool operator()(const Event* a, const Event* b) const {
    if (a->t != b->t) return a->t > b->t; // min-heap by time
    return a->id > b->id;
  }
};

class Simulator {
public:
  static Simulator& Instance() { static Simulator s; return s; }

  TimeUs Now() const { return m_now; }

  EventId Schedule(TimeUs at, std::function<void()> fn) {
    auto* e = new Event{at, ++m_nextId, std::move(fn), false};
    m_events.push(e);
    m_byId[e->id] = e;
    return e->id;
  }

  EventId ScheduleIn(TimeUs delay, std::function<void()> fn) {
    return Schedule(m_now + delay, std::move(fn));
  }

  void Cancel(EventId id) {
    auto it = m_byId.find(id);
    if (it != m_byId.end() && it->second) it->second->cancelled = true;
  }

  void Run(TimeUs until) {
    while (!m_events.empty()) {
      Event* e = m_events.top();
      if (!e) { m_events.pop(); continue; }
      if (e->cancelled) { CleanupTop(); continue; }
      if (e->t > until) break;

      m_events.pop();
      m_byId.erase(e->id);
      m_now = e->t;

      auto fn = std::move(e->fn);
      delete e;
      fn();
    }
    m_now = until;
    while (!m_events.empty() && m_events.top()->cancelled) CleanupTop();
  }

  void Reset() {
    while (!m_events.empty()) { delete m_events.top(); m_events.pop(); }
    m_byId.clear();
    m_now = 0;
    m_nextId = 0;
  }

private:
  void CleanupTop() {
    Event* e = m_events.top();
    m_events.pop();
    m_byId.erase(e->id);
    delete e;
  }

  TimeUs m_now{0};
  EventId m_nextId{0};
  std::priority_queue<Event*, std::vector<Event*>, EventCmp> m_events;
  std::unordered_map<EventId, Event*> m_byId;
};

}
