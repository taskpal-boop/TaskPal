#include "clock.h"

#include <M5Unified.h>
#include <sys/time.h>

#include <cstdlib>
#include <ctime>

namespace hal::clock {

namespace {
constexpr int kMinValidYear = 2025;

bool writeRtc(int64_t utc) {
  const time_t t = time_t(utc);
  tm g{};
  gmtime_r(&t, &g);
  M5.Rtc.setDateTime(&g);
  return true;
}
}  // namespace

void setTz(const std::string& tz) {
  setenv("TZ", tz.c_str(), 1);
  tzset();
}

bool begin(const std::string& tz) {
  setTz(tz);
  if (!M5.Rtc.isEnabled()) return false;
  const auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < kMinValidYear) return false;
  const core::Date d{dt.date.year, dt.date.month, dt.date.date};
  const int64_t utc = core::daysFromCivil(d) * 86400 + dt.time.hours * 3600 + dt.time.minutes * 60 + dt.time.seconds;
  timeval tv{time_t(utc), 0};
  settimeofday(&tv, nullptr);
  return true;
}

bool valid() {
  tm l{};
  const time_t t = time(nullptr);
  localtime_r(&t, &l);
  return l.tm_year + 1900 >= kMinValidYear;
}

int64_t epoch() { return int64_t(time(nullptr)); }

core::LocalTime now() {
  const time_t t = time(nullptr);
  tm l{};
  localtime_r(&t, &l);
  return core::LocalTime{core::Date{l.tm_year + 1900, l.tm_mon + 1, l.tm_mday}, l.tm_hour, l.tm_min};
}

int second() {
  const time_t t = time(nullptr);
  tm l{};
  localtime_r(&t, &l);
  return l.tm_sec;
}

bool setLocal(const core::LocalTime& t, int second) {
  tm l{};
  l.tm_year = t.date.y - 1900;
  l.tm_mon = t.date.m - 1;
  l.tm_mday = t.date.d;
  l.tm_hour = t.hour;
  l.tm_min = t.minute;
  l.tm_sec = second;
  l.tm_isdst = -1;  // let the TZ rules decide
  const time_t utc = mktime(&l);
  if (utc == time_t(-1)) return false;
  return setEpoch(int64_t(utc));
}

bool setEpoch(int64_t utcSeconds) {
  timeval tv{time_t(utcSeconds), 0};
  settimeofday(&tv, nullptr);
  return writeRtc(utcSeconds);
}

}  // namespace hal::clock
