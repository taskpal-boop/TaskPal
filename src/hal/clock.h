// Wall clock: the BM8563 RTC holds UTC; local time comes from a POSIX TZ string (ARCHITECTURE §8).
#pragma once
#include <cstdint>
#include <string>

#include "../core/date.h"

namespace hal::clock {

// Loads the system clock from the RTC and applies the time zone.
bool begin(const std::string& tz);
void setTz(const std::string& tz);

// False until the clock has been set once (RTC reads a year before 2025).
bool valid();

int64_t epoch();          // UTC seconds
core::LocalTime now();    // local wall-clock time (DST applied)
int second();             // local seconds, for display only

// Sets system time and RTC from a local wall-clock time.
bool setLocal(const core::LocalTime& t, int second = 0);
bool setEpoch(int64_t utcSeconds);  // e.g. after NTP

}  // namespace hal::clock
