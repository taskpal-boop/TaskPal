#include "date.h"

#include <cstdio>

namespace core {

// Howard Hinnant's days_from_civil / civil_from_days (public domain).
int64_t daysFromCivil(const Date& date) {
  int64_t y = date.y - (date.m <= 2 ? 1 : 0);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t mp = date.m + (date.m > 2 ? -3 : 9);
  const int64_t doy = (153 * mp + 2) / 5 + date.d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

Date civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  Date out;
  out.d = int(doy - (153 * mp + 2) / 5 + 1);
  out.m = int(mp < 10 ? mp + 3 : mp - 9);
  out.y = int(yoe + era * 400 + (out.m <= 2 ? 1 : 0));
  return out;
}

Date addDays(const Date& date, int days) { return civilFromDays(daysFromCivil(date) + days); }

int dayOfWeek(const Date& date) {
  const int64_t z = daysFromCivil(date);  // 1970-01-01 was a Thursday (4)
  return int(((z + 4) % 7 + 7) % 7);
}

bool operator<(const Date& a, const Date& b) { return daysFromCivil(a) < daysFromCivil(b); }

std::string toIso(const Date& date) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", date.y, date.m, date.d);
  return buf;
}

bool parseIso(const std::string& text, Date& out) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  Date d;
  d.y = std::stoi(text.substr(0, 4));
  d.m = std::stoi(text.substr(5, 2));
  d.d = std::stoi(text.substr(8, 2));
  if (d.m < 1 || d.m > 12 || d.d < 1 || d.d > 31) return false;
  if (civilFromDays(daysFromCivil(d)) != d) return false;  // rejects e.g. 2026-02-30
  out = d;
  return true;
}

}  // namespace core
