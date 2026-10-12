// E-ink panel wrapper: refresh modes, ghosting cleanup and the IT8951 hang watchdog.
// Rules measured in M1 (EINK_TUNING_LOG, D11):
//  - set the mode BEFORE drawing: M5GFX converts colours to panel levels at draw time;
//  - Fast/Fastest frames use pure black/white only; flat grays use theme::gray(L) in Quality/Text;
//  - never block the UI loop on a refresh: poll busy() and keep reading touch.
#pragma once
#include <M5Unified.h>

#include <cstdint>

namespace hal {

enum class Mode : uint8_t {
  Quality,  // GC16 ~450 ms, flashes; screen changes and cleanups
  Text,     // GL16 ~450 ms; grays changing in place (settle frame)
  Fast,     // DU ~240 ms; black/white feedback (flash, stamp, blackout)
  Fastest,  // DU4 ~140 ms; tap frame, key press, wiggle
};

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  Rect unite(const Rect& o) const;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class Epd {
 public:
  static constexpr int kWidth = 540;
  static constexpr int kHeight = 960;
  static constexpr uint32_t kCleanupAfterPartials = 40;  // SP-03: clean after 40 DU/DU4 cycles
  static constexpr uint32_t kCleanupIdleMs = 5000;
  static constexpr uint32_t kHungMs = 1500;               // a healthy call never takes this long

  // Call after M5.begin(): portrait, manual refresh, health probe (+ recovery).
  bool begin();

  M5GFX& gfx() { return M5.Display; }

  void setMode(Mode m);  // call before drawing
  Mode mode() const { return mode_; }

  // Starts refreshing `r` with mode `m`. Waits only for a previous refresh still running.
  void refresh(const Rect& r, Mode m);
  void refreshAll(Mode m) { refresh(Rect{0, 0, kWidth, kHeight}, m); }

  bool busy() { return gfx().displayBusy(); }
  void waitIdle() { gfx().waitDisplay(); }  // blocking: boot, sleep and tests only

  // Black -> white double GC16. Clears mid-gray ghosts a single GC16 leaves (SP-02).
  void deepClean();

  // Call from the UI loop with the time since the last touch. Runs a background GC16 of the
  // current image once kCleanupAfterPartials fast updates have piled up. Returns true if it did.
  bool maintain(uint32_t idleMs);

  // Watchdog (Q13): checks BUSY and probes the panel; if hung, resets the IT8951 and restarts the
  // ESP32 (at most 2 restarts in a row). Returns true if the panel is healthy.
  bool checkHealth();
  uint32_t recoveries() const { return recoveries_; }
  void forceRecovery() { recover(); }  // test hook (serial 'R' in main.cpp): restarts the device
  uint32_t lastBootRecovered() const { return lastBootRecovered_; }  // restarts needed at this boot
  uint32_t partials() const { return partials_; }

  // Set after a recovery: the panel lost its image memory, so the screen must redraw everything.
  bool takeRedrawRequest() {
    bool r = redraw_;
    redraw_ = false;
    return r;
  }

 private:
  void recover();

  Mode mode_ = Mode::Quality;
  uint32_t partials_ = 0;
  uint32_t recoveries_ = 0;
  uint32_t lastBootRecovered_ = 0;
  bool redraw_ = false;
};

Epd& epd();

}  // namespace hal
