// Non-blocking frame sequencer for the e-ink animations (DESIGN_SPEC §6, ARCHITECTURE §6).
// Each frame draws into panel memory, then refreshes one rect with one mode. The next frame
// starts once that refresh has finished AND holdMs has passed, so the loop keeps reading touch.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "../hal/epd.h"

namespace ui {

struct Frame {
  hal::Rect rect;               // area to refresh (empty = draw only / pure wait)
  hal::Mode mode;
  uint32_t holdMs;              // keep this frame on screen at least this long after it appears
  std::function<void()> draw;   // writes panel memory (e.g. Region::pushOnly); may be empty
  const char* name = "";        // for timing logs
};

class Sequencer {
 public:
  void play(std::vector<Frame> frames, std::function<void()> onDone = {});
  bool running() const { return running_; }
  void cancel();
  // Call every loop. Returns true while a sequence is running.
  bool tick(uint32_t nowMs);

 private:
  void start(size_t i, uint32_t nowMs);

  std::vector<Frame> frames_;
  std::function<void()> onDone_;
  size_t index_ = 0;
  bool running_ = false;
  bool waitingRefresh_ = false;
  uint32_t shownAt_ = 0;     // when the current frame's refresh finished
  uint32_t startedAt_ = 0;   // when the current frame started (for logs)
  uint32_t sequenceAt_ = 0;
};

}  // namespace ui
