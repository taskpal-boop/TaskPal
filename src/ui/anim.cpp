#include "anim.h"

#include <Arduino.h>

namespace ui {

void Sequencer::play(std::vector<Frame> frames, std::function<void()> onDone) {
  frames_ = std::move(frames);
  onDone_ = std::move(onDone);
  running_ = !frames_.empty();
  if (running_) {
    sequenceAt_ = millis();
    start(0, sequenceAt_);
  } else if (onDone_) {
    onDone_();
  }
}

void Sequencer::cancel() {
  running_ = false;
  frames_.clear();
  onDone_ = nullptr;
}

void Sequencer::start(size_t i, uint32_t nowMs) {
  index_ = i;
  startedAt_ = nowMs;
  const Frame& f = frames_[i];
  if (f.draw) f.draw();
  if (!f.rect.empty()) {
    hal::epd().refresh(f.rect, f.mode);
    waitingRefresh_ = true;
  } else {
    waitingRefresh_ = false;
    shownAt_ = nowMs;
  }
}

bool Sequencer::tick(uint32_t nowMs) {
  if (!running_) return false;
  if (waitingRefresh_) {
    if (hal::epd().busy()) return true;
    waitingRefresh_ = false;
    shownAt_ = nowMs;
    ESP_LOGI("anim", "frame %u %-8s shown at +%lu ms (refresh %lu ms)", unsigned(index_), frames_[index_].name,
             (unsigned long)(nowMs - sequenceAt_), (unsigned long)(nowMs - startedAt_));
  }
  if (nowMs - shownAt_ < frames_[index_].holdMs) return true;
  if (index_ + 1 < frames_.size()) {
    start(index_ + 1, nowMs);
    return true;
  }
  running_ = false;
  auto done = std::move(onDone_);
  onDone_ = nullptr;
  frames_.clear();
  if (done) done();
  return false;
}

}  // namespace ui
