#include "logic_day.h"

namespace core {

Date logicalDay(const LocalTime& now, int resetHour) {
  return now.hour < resetHour ? addDays(now.date, -1) : now.date;
}

Date weekStart(const Date& day, int weekStartDow) {
  const int back = (dayOfWeek(day) - weekStartDow + 7) % 7;
  return addDays(day, -back);
}

int stepHour(int hour, int delta) { return ((hour + delta) % 24 + 24) % 24; }

}  // namespace core
