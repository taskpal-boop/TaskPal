// Battery and sleep (D13, M8). Reference measurements: src/spikes/sleep (SP-06).
#pragma once
#include <cstdint>

namespace hal::power {

int batteryMv();
int batteryPercent();  // 0..100 (M5Unified estimate; see Q14)
// Median of `samples` voltage readings: one bad ADC read can't trigger a shutdown (Q14).
int batteryMvMedian(int samples = 5);

enum class Wake { Touch, Button, Timer, PowerOn, Other };
const char* describe(Wake w);

// Call once at boot, right after M5.begin(): releases the power-latch hold from deep sleep.
void onBoot();
// Why this boot happened (after a deep sleep: Touch / Button / Timer).
Wake bootWake();

// Light sleep with the panel asleep; wakes on touch, the side button or after `seconds`.
// The panel is awake again when this returns. The image stays on the e-ink the whole time.
Wake lightSleep(uint32_t seconds);

// Deep sleep: wakes (as a cold boot) on touch, the side button or after `seconds` (0 = no timer).
// Holds the main power latch (GPIO2) so the board stays powered on battery.
[[noreturn]] void deepSleep(uint32_t seconds);

// Low battery: power off. On USB the board stays powered, so this is a deep sleep that the side
// button wakes (M3 finding).
[[noreturn]] void powerOff();

}  // namespace hal::power
