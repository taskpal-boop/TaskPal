#include "power.h"

#include <M5Unified.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

#include "../core/now_list.h"
#include "../hal/clock.h"
#include "../hal/epd.h"
#include "../hal/net.h"
#include "../hal/storage.h"
#include "../hal/touch.h"
#include "../screens/kid.h"
#include "../ui/fonts.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "app.h"

namespace app::power {

namespace {

constexpr const char* kLogFile = "/power.log";
constexpr uint32_t kLightSleepSec = 30 * 60;   // D13 stage 2 after this much light sleep
// Low battery (D28). The battery measured 3604 mV going into deep sleep and was flat 5 h later
// (2026-10-02 power.log): below ~3.6 V a LiPo empties fast. Power off cleanly at kLowMv (the RTC
// keeps the time), and while under kWatchMv wake from deep sleep hourly to check.
constexpr int kLowMv = 3450;
constexpr int kWatchMv = 3700;
constexpr uint32_t kWatchSec = 3600;
constexpr int kLowChecksAwake = 2;             // awake: 2 low checks in a row (refreshes sag the reading)
constexpr uint32_t kBatteryCheckMs = 60000;
constexpr uint32_t kBatteryLogMs = 10 * 60000;

// Screen refresh while asleep (D30): wake this long after a NOW / greeting boundary, so the screen
// has changed when we draw it.
constexpr int kChangeMarginSec = 10;

// What the deep-sleep timer was set for (D28, D30).
enum class TimerFor : uint8_t { Reset, Refresh, Battery };

RTC_DATA_ATTR uint32_t gResumeList = 0;  // survives deep sleep: list to reopen on wake
RTC_DATA_ATTR TimerFor gTimerFor = TimerFor::Reset;
RTC_DATA_ATTR int64_t gTargetEpoch = 0;  // when the timer should have fired (the RTC timer runs ~0.4% fast)

bool gSleepAfterRender = false;
uint32_t gLastBatteryCheck = 0;
uint32_t gLastBatteryLog = 0;
int gLowCount = 0;

// Seconds until the next reset hour (local time, DST-aware via mktime).
uint32_t secondsUntilReset() {
  if (!hal::clock::valid()) return 24 * 3600;
  const time_t now = time(nullptr);
  tm l{};
  localtime_r(&now, &l);
  l.tm_hour = state().settings.resetHour;
  l.tm_min = 0;
  l.tm_sec = 5;  // a few seconds after, so the logical day has changed when we wake
  l.tm_isdst = -1;
  time_t t = mktime(&l);
  if (t <= now + 1) {
    l.tm_mday += 1;
    l.tm_isdst = -1;
    t = mktime(&l);
  }
  return uint32_t(std::max<time_t>(1, t - now));
}

// Seconds until Pick a list next looks different (a list window or the greeting changes, D30).
uint32_t secondsUntilChange() {
  if (!hal::clock::valid()) return 24 * 3600;
  const auto t = hal::clock::now();
  const int m = core::minutesToNextChange(state().lists, t.hour * 60 + t.minute);
  return uint32_t(std::max(1, m * 60 - hal::clock::second() + kChangeMarginSec));
}

bool screenBusy() {
  ui::Screen* s = ui::screens().current();
  return s && s->busy();
}

int batteryNow() { return hal::power::batteryMvMedian(15); }

[[noreturn]] void goDeep() {
  // No screen yet on a battery-check boot: keep the list we had (D28).
  if (ui::Screen* s = ui::screens().current()) gResumeList = s->isParentArea() ? 0 : s->resumeListId();
  const int mv = batteryNow();
  if (mv < kLowMv) lowBatteryNow();  // don't go to sleep only to die in it (D28)
  uint32_t secs = secondsUntilReset();
  gTimerFor = TimerFor::Reset;
  // A deep-sleep wake shows Pick a list unless a list is being resumed: wake when it would change
  // (D30). Within a minute of the reset hour, the reset wake redraws anyway.
  if (state().settings.autoRefresh && gResumeList == 0) {
    const uint32_t change = secondsUntilChange();
    if (change + 60 < secs) {
      secs = change;
      gTimerFor = TimerFor::Refresh;
    }
  }
  if (mv < kWatchMv && secs > kWatchSec) {
    secs = kWatchSec;
    gTimerFor = TimerFor::Battery;
  }
  gTargetEpoch = hal::clock::epoch() + secs;
  const char* why[] = {"", " refresh", " battery check"};
  log("deep sleep (resume list %u, timer %u s%s, batt %d mV)", unsigned(gResumeList), unsigned(secs),
      why[int(gTimerFor)], mv);
  Serial.flush();
  hal::power::deepSleep(secs);
}

void showAfterWake() {
  ui::Screen* s = ui::screens().current();
  if (s && s->isParentArea()) {
    ui::screens().show(screens::makePickList());  // the parent area needs the PIN again
  } else {
    ui::screens().redraw();
  }
}

// D13: light sleep (touch/side button wake in ~1.4 s), stepping through the reset hour, then deep.
void sleepCycle(uint32_t budget = kLightSleepSec) {
  const int mv = batteryNow();
  if (mv < kLowMv) lowBatteryNow();  // D28
  log("light sleep (batt %d mV, deep after %u s)", mv, unsigned(budget));
  Serial.flush();
  for (;;) {
    const uint32_t toReset = secondsUntilReset();
    ui::Screen* cur = ui::screens().current();
    const bool watchScreen = state().settings.autoRefresh && cur && cur->showsTime();  // D30
    const uint32_t toChange = watchScreen ? secondsUntilChange() : UINT32_MAX;
    const uint32_t seconds = std::max<uint32_t>(1, std::min({budget, toReset, toChange}));
    const int64_t t0 = hal::clock::epoch();
    const auto wake = hal::power::lightSleep(seconds);
    const uint32_t slept = uint32_t(std::max<int64_t>(0, hal::clock::epoch() - t0));
    budget = budget > slept ? budget - slept : 0;
    if (batteryNow() < kLowMv) lowBatteryNow();  // D28
    if (wake == hal::power::Wake::Timer) {
      if (slept + 5 >= toReset) {  // reset hour: new day on screen, then keep sleeping
        const bool changed = rolloverIfNeeded(true);
        log("reset-hour wake while asleep (new day: %s)", changed ? "yes" : "no");
        if (changed) {
          showAfterWake();
          ui::screens().loop(millis());  // applies a pending show() and renders
          hal::epd().waitIdle();
        }
      } else if (watchScreen && cur->stale()) {
        // The NOW pill or the greeting changed (D30). Woken a few seconds early (fast RTC timer)?
        // Then nothing is stale yet and the next loop sleeps the remaining seconds.
        log("screen refresh while asleep");
        ui::screens().redraw();
        ui::screens().loop(millis());
        hal::epd().waitIdle();
      }
      if (budget == 0) goDeep();
      continue;
    }
    // Touch, side button or anything else: back to normal use.
    log("woke from light sleep: %s after %u s", hal::power::describe(wake), unsigned(slept));
    hal::touch().suppressCurrentPress();
    showAfterWake();
    return;
  }
}

void drawLowBattery() {
  using namespace theme;
  ui::Region r(hal::Rect{0, 0, kScreenW, kScreenH});
  auto& g = r.canvas();
  // Battery outline with an almost empty fill.
  g.fillRoundRect(170, 300, 180, 90, 14, gray(kInk));
  g.fillRoundRect(178, 308, 164, 74, 10, gray(kWhite));
  g.fillRect(350, 325, 18, 40, gray(kInk));
  g.fillRect(186, 316, 26, 58, gray(kInk));
  ui::drawText(g, fonts::Id::fredoka_46, "Please charge me", kScreenW / 2, 470, lgfx::middle_center, kInk);
  ui::drawText(g, fonts::Id::atkinson_26b, "Plug in the USB cable, then", kScreenW / 2, 540, lgfx::middle_center,
               kText2);
  ui::drawText(g, fonts::Id::atkinson_26b, "press the side button.", kScreenW / 2, 576, lgfx::middle_center, kText2);
  r.push(hal::Mode::Quality);
  hal::epd().waitIdle();
}

}  // namespace

void log(const char* fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  const auto t = hal::clock::now();
  char line[200];
  std::snprintf(line, sizeof line, "%04d-%02d-%02d %02d:%02d:%02d %s", t.date.y, t.date.m, t.date.d, t.hour, t.minute,
                hal::clock::second(), msg);
  hal::storage::appendLine(kLogFile, line);
  Serial.printf("[power] %s\n", line);
}

std::unique_ptr<ui::Screen> bootScreen(hal::power::Wake wake) {
  const bool fromSleep = wake == hal::power::Wake::Touch || wake == hal::power::Wake::Button ||
                         wake == hal::power::Wake::Timer;
  if (fromSleep && gResumeList && core::findList(state().lists, gResumeList)) {
    return screens::makeChecklist(gResumeList);
  }
  return screens::makePickList();
}

void onBoot(hal::power::Wake wake) {
  const int mv = batteryNow();
  log("boot: %s (epd recovery restarts %u, batt %d mV, clock %s)", hal::power::describe(wake),
      unsigned(hal::epd().lastBootRecovered()), mv, hal::clock::valid() ? "ok" : "LOST");
  // Woken, or switched on, with an almost empty battery: say so and switch off (D28).
  if (mv < kLowMv) lowBatteryNow();
  if (wake == hal::power::Wake::Touch || wake == hal::power::Wake::Button) hal::touch().suppressCurrentPress();
  auto& s = state().settings;
  // The clock was lost (the battery ran completely flat): get the time back now, before the first
  // screen, instead of waiting for the reset hour (D28).
  if (!hal::clock::valid() && s.autoSync && hal::net::hasCredentials()) {
    const auto r = hal::net::syncSaved(s.tz);
    log("clock lost, time sync: %s", hal::net::describe(r));
    if (r == hal::net::SyncResult::Ok) {
      s.lastSync = hal::clock::epoch();
      saveSettings();
      rolloverIfNeeded(false);
    }
  }
  if (wake == hal::power::Wake::Timer && gTimerFor == TimerFor::Battery) {
    // Hourly battery check while the battery is low (D28): it is fine, so straight back to sleep
    // without drawing anything.
    goDeep();
  }
  if (wake == hal::power::Wake::Timer && gTimerFor == TimerFor::Refresh) {
    // Pick a list would look different now (D30). The RTC timer runs fast: if this is a few
    // seconds early, sleep the rest first. Otherwise draw the boot screen (Pick a list), then sleep.
    if (hal::clock::valid() && hal::clock::epoch() + 5 < gTargetEpoch) goDeep();
    log("screen refresh wake");
    gSleepAfterRender = true;
    return;
  }
  if (wake == hal::power::Wake::Timer) {
    // The reset hour. app::load() has already rolled progress over to the new day, so the daily
    // time sync can't hang off rolloverIfNeeded() here (it returned "no change" and skipped the sync,
    // M8 test): run it directly.
    rolloverIfNeeded(false);
    if (s.autoSync && hal::net::hasCredentials()) {
      const auto r = hal::net::syncSaved(s.tz);
      log("reset-hour time sync: %s", hal::net::describe(r));
      if (r == hal::net::SyncResult::Ok) {
        s.lastSync = hal::clock::epoch();
        saveSettings();
        rolloverIfNeeded(false);  // the real time may be a different logical day than the RTC said
      }
    }
    gSleepAfterRender = true;
  }
}

void tick(uint32_t nowMs) {
  auto& epd = hal::epd();
  // Timer wake at the reset hour: once the new day is drawn, straight back to deep sleep.
  if (gSleepAfterRender) {
    if (ui::screens().lastRenderMs() > 0 && !epd.busy()) {
      gSleepAfterRender = false;
      goDeep();
    }
    return;
  }
  // Battery: median voltage every minute; logged every 10 minutes for the soak (Q14).
  if (nowMs - gLastBatteryCheck >= kBatteryCheckMs) {
    gLastBatteryCheck = nowMs;
    const int mv = hal::power::batteryMvMedian();
    gLowCount = mv < kLowMv ? gLowCount + 1 : 0;
    if (nowMs - gLastBatteryLog >= kBatteryLogMs || gLastBatteryLog == 0) {
      gLastBatteryLog = nowMs;
      log("batt %d mV (%d %%)", mv, hal::power::batteryPercent());
    }
    if (gLowCount >= kLowChecksAwake) lowBatteryNow();
  }
  // Idle: 2 minutes (settings.sleepMin) without a touch, nothing animating, panel idle.
  const uint32_t idle = nowMs - hal::touch().lastActivityMs();
  if (idle >= uint32_t(state().settings.sleepMin) * 60000u && !screenBusy() && !epd.busy() &&
      !hal::touch().pressed()) {
    sleepCycle();
  }
}

void sleepNow() { sleepCycle(60); }  // test: deep sleep after only 60 s of light sleep

void lowBatteryNow() {
  log("low battery: %d mV, powering off", hal::power::batteryMvMedian());
  drawLowBattery();
  hal::power::powerOff();
}

}  // namespace app::power
