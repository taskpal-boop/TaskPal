#include "screen.h"

#include "../hal/epd.h"
#include "region.h"

namespace ui {

ScreenMgr& screens() {
  static ScreenMgr instance;
  return instance;
}

void ScreenMgr::renderAll() {
  const uint32_t t0 = millis();
  Region full(hal::Rect{0, 0, hal::Epd::kWidth, hal::Epd::kHeight});
  if (full.ok()) {
    current_->render(full.canvas());
    full.push(hal::Mode::Quality);  // GC16: a GL16 screen change left the screen unclean (D24, rejected)
  } else {
    // No PSRAM for a full canvas: draw straight to the panel (slow, but correct).
    auto& epd = hal::epd();
    epd.setMode(hal::Mode::Quality);
    epd.gfx().fillScreen(TFT_WHITE);
    current_->render(epd.gfx());
    epd.refreshAll(hal::Mode::Quality);
  }
  lastRenderMs_ = millis() - t0;
  ESP_LOGI("screen", "full render %lu ms", (unsigned long)lastRenderMs_);
}

void ScreenMgr::loop(uint32_t nowMs) {
  auto& epd = hal::epd();
  if (next_) {
    current_ = std::move(next_);
    hal::touch().clear();  // taps meant for the old screen must not hit the new one
    redraw_ = true;
  }
  if (!current_) return;
  if (epd.takeRedrawRequest()) redraw_ = true;  // panel was reset by the watchdog
  if (redraw_) {
    redraw_ = false;
    renderAll();
    return;
  }
  hal::TouchEvent e;
  while (hal::touch().poll(e)) {
    current_->onTouch(e);
    if (next_ || redraw_) return;  // the screen asked to leave or redraw: handle it first
  }
  current_->onTick(nowMs);
  epd.maintain(nowMs - hal::touch().lastActivityMs());
}

}  // namespace ui
