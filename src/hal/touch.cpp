#include "touch.h"

#include <M5Unified.h>

#include <cstdlib>

namespace hal {

Touch& touch() {
  static Touch instance;
  return instance;
}

void Touch::push(const TouchEvent& e) {
  const int next = (tail_ + 1) % kQueue;
  if (next == head_) head_ = (head_ + 1) % kQueue;  // full: drop the oldest
  queue_[tail_] = e;
  tail_ = next;
}

bool Touch::poll(TouchEvent& out) {
  if (head_ == tail_) return false;
  out = queue_[head_];
  head_ = (head_ + 1) % kQueue;
  return true;
}

void Touch::suppressCurrentPress() {
  M5.update();
  suppress_ = M5.Touch.getCount() > 0;
  down_ = false;
  lastActivityMs_ = millis();  // waking counts as activity: restart the idle timer
  clear();
}

void Touch::update(uint32_t nowMs) {
  const bool isDown = M5.Touch.getCount() > 0;
  if (suppress_) {  // swallow the waking press until the finger lifts
    if (!isDown) suppress_ = false;
    lastActivityMs_ = nowMs;
    return;
  }
  if (isDown) {
    const auto& d = M5.Touch.getDetail(0);
    lastX_ = d.x;
    lastY_ = d.y;
    lastActivityMs_ = nowMs;
    if (!down_) {
      down_ = true;
      holdFired_ = moved_ = false;
      downX_ = d.x;
      downY_ = d.y;
      downMs_ = nowMs;
      push({TouchEvent::Type::Down, downX_, downY_, 0});
      return;
    }
    if (std::abs(d.x - downX_) > kTapSlopPx || std::abs(d.y - downY_) > kTapSlopPx) moved_ = true;
    if (!holdFired_ && !moved_ && nowMs - downMs_ >= holdMs_) {
      holdFired_ = true;
      push({TouchEvent::Type::Hold, downX_, downY_, nowMs - downMs_});
    }
    return;
  }
  if (down_) {
    down_ = false;
    lastActivityMs_ = nowMs;
    const uint32_t held = nowMs - downMs_;
    if (!holdFired_ && !moved_) push({TouchEvent::Type::Tap, downX_, downY_, held});
    push({TouchEvent::Type::Up, lastX_, lastY_, held});
  }
}

}  // namespace hal
