#pragma once
#include "Types.h"
#include "Packet.h"
#include <cstdint>
#include <vector>
#include <algorithm>

namespace wifi {

// Простая BA-сессия на один поток (TX->RX, один TID/AC).
// Окно фиксированного размера (например, 64). BA возвращает seq для ретраев.
class BlockAckSession {
public:
  explicit BlockAckSession(uint16_t winSize = 64)
    : m_winSize(winSize),
      m_base(0),
      m_window(winSize, false) {}

  uint16_t WindowSize() const { return m_winSize; }
  uint16_t Base() const { return m_base; }

  // Вызывается при формировании MPDU: регистрируем seq в окне (как "ожидаемый ACK").
  void OnMpduQueued(uint16_t seq) {
    if (m_window.empty()) {
      m_window.assign(m_winSize, false);
      m_base = seq;
    }
    // Пока не отмечаем здесь ack; ack придёт через bitmap от RX.
  }

  // После приёма A‑MPDU на RX вызываем OnBlockAck на TX, передавая ssn и bitmap.
  // Возвращаем список seq, которые надо ретранслировать.
  std::vector<uint16_t> OnBlockAck(uint16_t ssn, const std::vector<bool>& bitmap) {
    std::vector<uint16_t> needRetrans;
    if (bitmap.empty()) return needRetrans;

    // Окно BA интерпретируем как [ssn, ssn + bitmap.size()).
    for (size_t i = 0; i < bitmap.size(); ++i) {
      uint16_t seq = uint16_t(ssn + i);
      bool ok = bitmap[i];
      if (!ok) {
        needRetrans.push_back(seq);
      } else {
        // Успешно подтверждён — можно было бы сдвигать окно, но в шаге 1 можно опустить.
      }
    }

    return needRetrans;
  }

private:
  uint16_t m_winSize{64};
  uint16_t m_base{0};
  std::vector<bool> m_window;
};

// Простейший буфер упорядочивания на RX (шаг 1: просто накапливаем и отдаём наверх).
class ReorderBuffer {
public:
  void OnMpduReceived(uint16_t /*seq*/, Packet* pkt) {
    delivered.push_back(pkt);
  }

  std::vector<Packet*> TakeDelivered() {
    std::vector<Packet*> out;
    out.swap(delivered);
    return out;
  }

private:
  std::vector<Packet*> delivered;
};

} // namespace wifi
