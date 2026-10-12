// Screen interface and manager. A screen change is a full GC16 render (ARCHITECTURE §4);
// screens do their own partial updates through ui::Region (D16).
#pragma once
#include <M5Unified.h>

#include <cstdint>
#include <memory>

#include "../hal/touch.h"

namespace ui {

class Screen {
 public:
  virtual ~Screen() = default;
  // Draw everything into `g`: a full-screen gray8 canvas already cleared to white. The manager
  // pushes it in one go and refreshes with GC16.
  virtual void render(lgfx::LovyanGFX& g) = 0;
  virtual void onTouch(const hal::TouchEvent& e) { (void)e; }
  virtual void onTick(uint32_t nowMs) { (void)nowMs; }

  // Sleep and wake (M8): the kid list to come back to after a deep sleep (0 = Pick a list), whether
  // the screen belongs to the parent area (never restored without the PIN), and whether it is in
  // the middle of something (an animation) so the device must not fall asleep yet.
  virtual uint32_t resumeListId() const { return 0; }
  virtual bool isParentArea() const { return false; }
  virtual bool busy() const { return false; }
  // Screen refresh while asleep (D30): whether the screen shows time-dependent content (greeting,
  // NOW pill), and whether that content is out of date right now.
  virtual bool showsTime() const { return false; }
  virtual bool stale() const { return false; }
};

class ScreenMgr {
 public:
  // Takes effect at the next loop(), so a screen may replace itself from onTouch.
  void show(std::unique_ptr<Screen> next) { next_ = std::move(next); }
  void redraw() { redraw_ = true; }
  Screen* current() { return current_.get(); }
  uint32_t lastRenderMs() const { return lastRenderMs_; }

  // Call every loop after hal::touch().update().
  void loop(uint32_t nowMs);
  // Draws the current screen right away (used while the loop is not running, e.g. at the reset
  // hour during sleep).
  void renderNow() {
    if (current_) renderAll();
  }

 private:
  void renderAll();

  std::unique_ptr<Screen> current_, next_;
  bool redraw_ = false;
  uint32_t lastRenderMs_ = 0;
};

ScreenMgr& screens();

}  // namespace ui
