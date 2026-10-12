#include "power.h"

#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <algorithm>
#include <vector>

#include "../core/battery.h"

namespace hal::power {

namespace {
constexpr gpio_num_t kMainPower = GPIO_NUM_2;    // M5Paper main power latch
constexpr gpio_num_t kSideButton = GPIO_NUM_38;  // side switch push, active low

Wake fromCause(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_EXT0: return Wake::Touch;
    case ESP_SLEEP_WAKEUP_EXT1: return Wake::Button;
    case ESP_SLEEP_WAKEUP_TIMER: return Wake::Timer;
    case ESP_SLEEP_WAKEUP_UNDEFINED: return Wake::PowerOn;
    default: return Wake::Other;
  }
}
}  // namespace

int batteryMv() { return M5.Power.getBatteryVoltage(); }
// From the voltage with a LiPo curve (D31); M5Unified's level is linear and reads far too high.
int batteryPercent() { return core::batteryPercent(batteryMvMedian()); }

int batteryMvMedian(int samples) {
  std::vector<int> v;
  for (int i = 0; i < samples; ++i) {
    v.push_back(M5.Power.getBatteryVoltage());
    delay(5);
  }
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

const char* describe(Wake w) {
  switch (w) {
    case Wake::Touch: return "touch";
    case Wake::Button: return "side button";
    case Wake::Timer: return "timer";
    case Wake::PowerOn: return "power-on";
    default: return "other";
  }
}

void onBoot() {
  gpio_hold_dis(kMainPower);
  gpio_deep_sleep_hold_dis();
}

Wake bootWake() { return fromCause(esp_sleep_get_wakeup_cause()); }

Wake lightSleep(uint32_t seconds) {
  M5.Display.waitDisplay();
  // Standby, not sleep: waking the IT8951 from sleep re-inits it and that logged "can't read data"
  // with a harsher GC16 flash afterwards (M8). Standby resumes without re-init.
  M5.Display.powerSaveOn();
  esp_sleep_enable_ext1_wakeup(1ULL << kSideButton, ESP_EXT1_WAKEUP_ALL_LOW);
  M5.Power.lightSleep(uint64_t(seconds) * 1000000ULL, true);  // + touch (ext0 GPIO36) + timer
  const Wake w = fromCause(esp_sleep_get_wakeup_cause());
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
  M5.Display.powerSaveOff();
  return w;
}

void deepSleep(uint32_t seconds) {
  M5.Display.waitDisplay();
  esp_sleep_enable_ext1_wakeup(1ULL << kSideButton, ESP_EXT1_WAKEUP_ALL_LOW);
  // Keep the main power latch high through deep sleep, or the board turns off on battery (SP-06).
  gpio_hold_en(kMainPower);
  gpio_deep_sleep_hold_en();
  M5.Power.deepSleep(uint64_t(seconds) * 1000000ULL, true);  // sleeps the panel, touch = ext0
  for (;;) {
  }
}

void powerOff() {
  M5.Display.waitDisplay();
  esp_sleep_enable_ext1_wakeup(1ULL << kSideButton, ESP_EXT1_WAKEUP_ALL_LOW);
  M5.Power.powerOff();
  for (;;) {
  }
}

}  // namespace hal::power
