// Sleep policy (D13, M8): 2 min idle -> light sleep; 30 more min -> deep sleep; a timer wake at the
// reset hour starts the new day; low battery -> "charge me" + power off. Events are logged to
// /power.log for the 48 h soak (PWR-07, Q13, Q14).
#pragma once
#include <cstdint>
#include <memory>

#include "../hal/power.h"
#include "../ui/screen.h"

namespace app::power {

// Screen to show after this boot (the list the child was on before a deep sleep, else Pick a list).
std::unique_ptr<ui::Screen> bootScreen(hal::power::Wake wake);

// Call once after setup: logs the boot and, for a reset-hour timer wake, arranges to go straight
// back to sleep once the new day is on screen.
void onBoot(hal::power::Wake wake);

// Call every loop.
void tick(uint32_t nowMs);

// Test hooks (serial commands). sleepNow: light sleep with only a 60 s budget before deep sleep.
void sleepNow();
void lowBatteryNow();
void log(const char* fmt, ...);

}  // namespace app::power
