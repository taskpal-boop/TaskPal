// Battery level from the measured voltage (D31). Host-testable: no Arduino headers.
#pragma once

namespace core {

// The device switches itself off below this (D28), so it counts as 0%.
constexpr int kBatteryEmptyMv = 3450;

// Estimated charge 0..100 from a resting/median battery voltage. A LiPo discharge curve (flat in
// the middle, steep below ~3.7 V) ending at kBatteryEmptyMv, checked against the 4.5-day run in
// power.log (2026-09-28 .. 10-02). M5Unified's linear percentage read 42% with ~8 h left.
int batteryPercent(int mv);

}  // namespace core
