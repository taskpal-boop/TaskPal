#include "battery.h"

namespace core {

namespace {
struct Point {
  int mv;
  int pct;
};
// Highest voltage first. Typical LiPo open-circuit curve, rescaled so 3450 mV (D28 cut-off) = 0%.
constexpr Point kCurve[] = {
    {4150, 100}, {4050, 90}, {3970, 80}, {3900, 70}, {3840, 60}, {3800, 50},
    {3770, 40},  {3740, 30}, {3700, 20}, {3620, 10}, {kBatteryEmptyMv, 0},
};
}  // namespace

int batteryPercent(int mv) {
  if (mv >= kCurve[0].mv) return 100;
  for (unsigned i = 1; i < sizeof kCurve / sizeof kCurve[0]; ++i) {
    const Point hi = kCurve[i - 1], lo = kCurve[i];
    if (mv >= lo.mv) return lo.pct + (mv - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv);
  }
  return 0;
}

}  // namespace core
