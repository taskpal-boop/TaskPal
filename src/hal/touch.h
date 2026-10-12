// Touch gestures on top of M5.Touch (GT911): down / tap / hold / up, queued so taps that arrive
// during a refresh or an animation are never lost (SP-07: 12/12 taps while polling).
#pragma once
#include <cstdint>

namespace hal {

struct TouchEvent {
  enum class Type : uint8_t {
    Down,  // finger landed: instant feedback (DU4 tap frame)
    Tap,   // released near where it landed, before the hold time
    Hold,  // still pressed after holdMs (fires once per press)
    Up,    // finger lifted (after Tap or Hold, or a cancelled drag)
  };
  Type type;
  int16_t x;       // where the finger landed (Down/Tap/Hold) or lifted (Up)
  int16_t y;
  uint32_t heldMs; // press duration so far (Hold) or in total (Tap/Up)
};

class Touch {
 public:
  static constexpr uint32_t kDefaultHoldMs = 3000;  // parent settings gear (DESIGN_SPEC §5.1)
  static constexpr int kTapSlopPx = 30;

  // Call once per loop, after M5.update().
  void update(uint32_t nowMs);
  bool poll(TouchEvent& out);  // dequeue the next event
  void clear() { head_ = tail_ = 0; }

  bool pressed() const { return down_; }
  uint32_t heldMs(uint32_t nowMs) const { return down_ ? nowMs - downMs_ : 0; }
  int16_t downX() const { return downX_; }
  int16_t downY() const { return downY_; }
  uint32_t lastActivityMs() const { return lastActivityMs_; }
  void setHoldMs(uint32_t ms) { holdMs_ = ms; }
  // The finger that woke the device must not also act (e.g. check a task): ignore the press that is
  // down right now until it is lifted. No effect if nothing is touching the screen.
  void suppressCurrentPress();

 private:
  void push(const TouchEvent& e);

  static constexpr int kQueue = 16;
  TouchEvent queue_[kQueue]{};
  int head_ = 0, tail_ = 0;
  bool down_ = false, holdFired_ = false, moved_ = false, suppress_ = false;
  int16_t downX_ = 0, downY_ = 0, lastX_ = 0, lastY_ = 0;
  uint32_t downMs_ = 0, lastActivityMs_ = 0;
  uint32_t holdMs_ = kDefaultHoldMs;
};

Touch& touch();

}  // namespace hal
