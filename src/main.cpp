// TaskPal for M5Paper. M4: kid screens on top of the HAL (M3) and core logic (M2).
#include <Arduino.h>
#include <M5Unified.h>

#include <sys/time.h>

#include <cstdio>
#include <string>

#include "app/app.h"
#include "app/power.h"
#include "core/json_codec.h"
#include "core/model.h"
#include "hal/clock.h"
#include "hal/epd.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/touch.h"
#include "screens/kid.h"
#include "ui/fonts.h"
#include "ui/region.h"
#include "ui/widgets.h"
#include "ui/screen.h"

namespace {

// Serial test commands, one per line (see README.md §5):
//   T HH:MM   set the local time today (keeps the date)     D YYYY-MM-DD  set the date
//   C         clear today's checks (stars kept)              S             dump settings/lists/progress
//   R         force a display recovery (watchdog test)       P             reload the Pick-a-list screen
//   A         add 2 test tasks to the first list (8 total)   X             remove tasks beyond 6 again
//   Z         clear the clock (RTC 2000-01-01) for TM-02
//   Y         sleep now (M8)                                 V             battery reading (Q14)
//   G         print /power.log                               K             low-battery screen + power off
//   W         400 back-to-back progress saves (PWR-06)
void handleCommand(const std::string& line) {
  if (line.empty()) return;
  auto& s = app::state();
  switch (line[0]) {
    case 'T': {
      int h = 0, m = 0;
      if (std::sscanf(line.c_str() + 1, " %d:%d", &h, &m) == 2) {
        core::LocalTime t = hal::clock::now();
        t.hour = h;
        t.minute = m;
        hal::clock::setLocal(t);
        app::rolloverIfNeeded();
        ui::screens().redraw();
        Serial.printf("[cmd] time set to %02d:%02d\n", h, m);
      }
      break;
    }
    case 'D': {
      core::LocalTime t = hal::clock::now();
      if (std::sscanf(line.c_str() + 1, " %d-%d-%d", &t.date.y, &t.date.m, &t.date.d) == 3) {
        hal::clock::setLocal(t);
        app::rolloverIfNeeded();
        ui::screens().redraw();
        Serial.printf("[cmd] date set to %04d-%02d-%02d\n", t.date.y, t.date.m, t.date.d);
      }
      break;
    }
    case 'C':
      s.progress.done.clear();
      app::saveProgress();
      ui::screens().show(screens::makePickList());
      Serial.println("[cmd] today's checks cleared");
      break;
    case 'S':
      Serial.printf("[cmd] settings %s\n", core::encodeSettings(s.settings).c_str());
      Serial.printf("[cmd] lists %s\n", core::encodeLists(s.lists).c_str());
      Serial.printf("[cmd] progress %s\n", core::encodeProgress(s.progress).c_str());
      break;
    case 'R': {
      const uint32_t t0 = millis();
      hal::epd().forceRecovery();
      Serial.printf("[cmd] forced EPD recovery in %lu ms, recoveries=%lu\n", (unsigned long)(millis() - t0),
                    (unsigned long)hal::epd().recoveries());
      break;
    }
    case 'P':
      ui::screens().show(screens::makePickList());
      break;
    case 'Y':  // M8: go to sleep now (light sleep; deep after 30 min)
      Serial.println("[cmd] sleeping now");
      app::power::sleepNow();
      break;
    case 'V':  // M8 / Q14: battery reading
      Serial.printf("[cmd] battery %d mV (median %d mV), %d %%\n", hal::power::batteryMv(),
                    hal::power::batteryMvMedian(), hal::power::batteryPercent());
      break;
    case 'G': {  // M8: print the power log
      std::string text;
      if (hal::storage::read("/power.log", text)) Serial.print(text.c_str());
      Serial.println("[cmd] end of power log");
      break;
    }
    case 'W':  // PWR-06: continuous saves so a reset lands mid-write
      Serial.println("[cmd] stress saves start");
      app::stressSaves(400);
      Serial.println("[cmd] stress saves done");
      break;
    case 'K':  // PWR-05: show the low-battery screen and power off
      app::power::lowBatteryNow();
      break;
    case 'Z': {  // TM-02: make the clock "not set" (RTC to 2000-01-01, system time to 0)
      tm t{};
      t.tm_year = 100;
      t.tm_mday = 1;
      M5.Rtc.setDateTime(&t);
      timeval tv{0, 0};
      settimeofday(&tv, nullptr);
      ui::screens().show(screens::makePickList());
      Serial.println("[cmd] clock cleared (RTC 2000-01-01)");
      break;
    }
    case 'A':  // UI-05 / UI-10: grow the first list to 8 tasks (long label included)
      if (!s.lists.lists.empty()) {
        const uint32_t id = s.lists.lists[0].id;
        core::addTask(s.lists, id, "Feed the cat", "star");
        core::addTask(s.lists, id, "Practice the piano", "music");
        app::saveLists();
        ui::screens().redraw();
        Serial.printf("[cmd] first list now has %u tasks\n", unsigned(s.lists.lists[0].tasks.size()));
      }
      break;
    case 'X':  // undo 'A': drop tasks beyond the first 6 of the first list
      if (!s.lists.lists.empty()) {
        auto& l = s.lists.lists[0];
        while (l.tasks.size() > 6) core::removeTask(s.lists, s.progress, l.id, l.tasks.back().id);
        app::saveLists();
        app::saveProgress();
        ui::screens().redraw();
        Serial.printf("[cmd] first list back to %u tasks\n", unsigned(l.tasks.size()));
      }
      break;
  }
}

void pollSerial() {
  static std::string line;
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line.clear();
    } else if (line.size() < 40) {
      line.push_back(c);
    }
  }
}

// ADM-04: holding the side button in (push) for 10 s while the device starts clears the parent
// PIN (lists and progress are kept). The same press also powers the M5Paper on.
void pinRecoveryAtBoot() {
  M5.update();
  if (!M5.BtnB.isPressed() || app::state().settings.pinHash.empty()) return;
  auto message = [](const char* line1, const char* line2) {
    ui::Region r(hal::Rect{0, 0, 540, 960});
    ui::drawText(r.canvas(), fonts::Id::fredoka_34, line1, 270, 440, lgfx::middle_center, 0);
    ui::drawText(r.canvas(), fonts::Id::atkinson_22b, line2, 270, 500, lgfx::middle_center, 3);
    r.push(hal::Mode::Quality);
    hal::epd().waitIdle();
  };
  message("Reset parent PIN?", "Keep holding the side button for 10 s");
  const uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    M5.update();
    if (!M5.BtnB.isPressed()) return;  // released early: normal start
    delay(20);
  }
  app::state().settings.pinSalt.clear();
  app::state().settings.pinHash.clear();
  app::saveSettings();
  Serial.println("[taskpal] parent PIN cleared by side-button hold");
  message("PIN reset", "Create a new PIN in the parent area");
  delay(2000);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = false;  // the screen manager draws the first screen with GC16
  M5.begin(cfg);
  hal::power::onBoot();  // release the deep-sleep power-latch hold
  const auto wake = hal::power::bootWake();

  const bool epdOk = hal::epd().begin();
  const bool storageOk = hal::storage::begin();
  int tmp = 0;
  if (storageOk) {
    tmp = hal::storage::removeTempFiles();
    hal::storage::removeDir("/fonts");   // left by the M1 spike (fonts are in flash now, D15)
    hal::storage::remove("/demo.json");  // M3 demo
  }
  const bool clockOk = hal::clock::begin("UTC0");
  app::load();  // applies the TZ from settings
  const bool psram = ui::loadFonts();

  if (wake == hal::power::Wake::PowerOn) pinRecoveryAtBoot();  // not on a side-button wake from sleep

  Serial.printf("[taskpal] boot (%s): epd %s (recovery restarts %lu), storage %s (%d tmp removed), clock %s, fonts %s\n",
                hal::power::describe(wake), epdOk ? "ok" : "HUNG", (unsigned long)hal::epd().lastBootRecovered(),
                storageOk ? "ok" : "FAILED", tmp, clockOk ? "ok" : "not set", psram ? "PSRAM" : "flash");
  ui::screens().show(app::power::bootScreen(wake));  // back to the child's list after a deep sleep
  app::power::onBoot(wake);
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  hal::touch().update(now);
  ui::screens().loop(now);
  pollSerial();
  app::power::tick(now);  // idle sleep, reset-hour wake, low battery (M8)
  delay(5);
}
