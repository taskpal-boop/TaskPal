// The "logical day": lists reset at settings.resetHour, not at midnight (DESIGN_SPEC §7.2).
#pragma once
#include "date.h"

namespace core {

// Calendar date of (now - resetHour): before the reset hour it is still "yesterday".
Date logicalDay(const LocalTime& now, int resetHour);

// First day of the week containing `day`. weekStartDow: 0 = Sunday, 1 = Monday (default).
Date weekStart(const Date& day, int weekStartDow);

// Reset-hour stepper in Parent settings: 1-hour steps that wrap 23 <-> 0 (UT-20).
int stepHour(int hour, int delta);

// Minutes since local midnight.
inline int minuteOfDay(const LocalTime& t) { return t.hour * 60 + t.minute; }

}  // namespace core
