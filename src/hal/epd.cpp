#include "epd.h"

#include <esp_system.h>

#include <algorithm>

namespace hal {

namespace {
constexpr int kEpdResetPin = 23;  // IT8951 reset (M5GFX cfg.pin_rst)
constexpr uint32_t kMaxRecoveryRestarts = 2;
// Survives esp_restart (not power loss): consecutive recovery restarts, to avoid a boot loop.
RTC_NOINIT_ATTR uint32_t gRecoveryRestarts;

epd_mode_t toM5(Mode m) {
  switch (m) {
    case Mode::Text: return epd_mode_t::epd_text;
    case Mode::Fast: return epd_mode_t::epd_fast;
    case Mode::Fastest: return epd_mode_t::epd_fastest;
    default: return epd_mode_t::epd_quality;
  }
}
}  // namespace

Rect Rect::unite(const Rect& o) const {
  if (empty()) return o;
  if (o.empty()) return *this;
  const int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
  const int x1 = std::max(x + w, o.x + o.w), y1 = std::max(y + h, o.y + o.h);
  return Rect{int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

Epd& epd() {
  static Epd instance;
  return instance;
}

bool Epd::begin() {
  if (esp_reset_reason() != ESP_RST_SW) gRecoveryRestarts = 0;  // RTC_NOINIT is garbage after power-on
  gfx().setRotation(0);  // portrait 540x960, upright (HW-05)
  gfx().setAutoDisplay(false);
  return checkHealth();
}

void Epd::setMode(Mode m) {
  mode_ = m;
  gfx().setEpdMode(toM5(m));
}

void Epd::refresh(const Rect& r, Mode m) {
  if (r.empty()) return;
  setMode(m);
  const uint32_t t0 = millis();
  gfx().display(r.x, r.y, r.w, r.h);
  if (millis() - t0 > kHungMs) {
    ESP_LOGW("epd", "display() took %lu ms: panel hung", (unsigned long)(millis() - t0));
    recover();
    return;
  }
  if (m == Mode::Fast || m == Mode::Fastest) {
    ++partials_;
  } else if (r.w >= kWidth && r.h >= kHeight) {
    partials_ = 0;  // a full GC16/GL16 pass cleans the whole panel
  }
}

void Epd::deepClean() {
  setMode(Mode::Quality);
  gfx().fillScreen(TFT_BLACK);
  refreshAll(Mode::Quality);
  waitIdle();
  gfx().fillScreen(TFT_WHITE);
  refreshAll(Mode::Quality);
  waitIdle();
  partials_ = 0;
}

bool Epd::maintain(uint32_t idleMs) {
  if (partials_ < kCleanupAfterPartials || idleMs < kCleanupIdleMs || busy()) return false;
  // The IT8951 still holds the current image: refreshing it in GC16 removes the ghosting
  // without redrawing anything.
  refreshAll(Mode::Quality);
  return true;
}

bool Epd::checkHealth() {
  // A hung IT8951 holds BUSY forever: read the line first (fast), then probe with a real write.
  const uint32_t t0 = millis();
  while (busy() && millis() - t0 < kHungMs) delay(5);
  if (busy()) {
    ESP_LOGW("epd", "BUSY stuck for %lu ms: panel hung", (unsigned long)(millis() - t0));
    recover();  // restarts; only returns when the restart budget is used up
    return false;
  }
  const uint32_t t1 = millis();
  setMode(Mode::Quality);
  gfx().fillRect(0, 0, 4, 4, TFT_WHITE);  // any IT8951 write waits for BUSY
  if (millis() - t1 > kHungMs) {
    ESP_LOGW("epd", "health probe took %lu ms: panel hung", (unsigned long)(millis() - t1));
    recover();
    return false;
  }
  if (gRecoveryRestarts > 0) {
    ESP_LOGW("epd", "panel healthy after %lu recovery restart(s)", (unsigned long)gRecoveryRestarts);
    lastBootRecovered_ = gRecoveryRestarts;
  }
  gRecoveryRestarts = 0;
  return true;
}

void Epd::recover() {
  // Re-initialising M5GFX in place left the panel half-configured (garbage on screen, M5 test),
  // so recovery is: reset the IT8951, then restart the ESP32 through the normal boot path.
  // All user data is saved on every change, so a restart loses nothing.
  ++recoveries_;
  pinMode(kEpdResetPin, OUTPUT);
  digitalWrite(kEpdResetPin, LOW);
  delay(50);
  digitalWrite(kEpdResetPin, HIGH);
  if (gRecoveryRestarts < kMaxRecoveryRestarts) {
    ++gRecoveryRestarts;
    ESP_LOGW("epd", "panel hung: IT8951 reset + restart (%lu of %lu)", (unsigned long)gRecoveryRestarts,
             (unsigned long)kMaxRecoveryRestarts);
    delay(50);
    esp_restart();
  }
  ESP_LOGE("epd", "panel still hung after %lu restarts: giving up", (unsigned long)gRecoveryRestarts);
  gRecoveryRestarts = 0;  // next power-on starts a fresh budget
}

}  // namespace hal
