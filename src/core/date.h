// Civil-date helpers with no time-zone library: the caller supplies local wall-clock time
// (already DST-adjusted by the platform), and all TaskPal day logic works on that.
#pragma once
#include <cstdint>
#include <string>

namespace core {

struct Date {
  int y = 1970;
  int m = 1;  // 1..12
  int d = 1;  // 1..31
};

inline bool operator==(const Date& a, const Date& b) { return a.y == b.y && a.m == b.m && a.d == b.d; }
inline bool operator!=(const Date& a, const Date& b) { return !(a == b); }

// Local wall-clock time as shown on the device.
struct LocalTime {
  Date date;
  int hour = 0;    // 0..23
  int minute = 0;  // 0..59
};

int64_t daysFromCivil(const Date& date);  // days since 1970-01-01
Date civilFromDays(int64_t days);
Date addDays(const Date& date, int days);
int dayOfWeek(const Date& date);  // 0 = Sunday ... 6 = Saturday
bool operator<(const Date& a, const Date& b);

std::string toIso(const Date& date);                  // "YYYY-MM-DD"
bool parseIso(const std::string& text, Date& out);    // strict "YYYY-MM-DD"

}  // namespace core
