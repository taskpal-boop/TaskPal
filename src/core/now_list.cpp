#include "now_list.h"

#include <cstdio>

#include "text_rules.h"

namespace core {

bool inWindow(int fromMin, int toMin, int minuteOfDay) {
  if (fromMin < 0 || toMin < 0 || fromMin == toMin) return false;
  if (fromMin < toMin) return minuteOfDay >= fromMin && minuteOfDay < toMin;
  return minuteOfDay >= fromMin || minuteOfDay < toMin;
}

int nowListIndex(const ListSet& set, int minuteOfDay) {
  for (size_t i = 0; i < set.lists.size(); ++i) {
    const auto& l = set.lists[i];
    if (!l.anyTime() && inWindow(l.fromMin, l.toMin, minuteOfDay)) return int(i);
  }
  return -1;
}

std::string greeting(int hour, const std::string& childName) {
  const char* base = (hour >= 4 && hour < 12) ? "Good morning" : (hour >= 12 && hour < 18) ? "Good afternoon"
                                                                                           : "Good evening";
  const std::string name = trimmed(childName);
  return name.empty() ? std::string(base) + "!" : std::string(base) + ", " + name + "!";
}

int minutesToNextChange(const ListSet& set, int minuteOfDay) {
  int best = 1440;
  auto consider = [&](int boundary) {
    int d = ((boundary - minuteOfDay) % 1440 + 1440) % 1440;
    if (d == 0) d = 1440;  // happening now: the next one is tomorrow
    if (d < best) best = d;
  };
  consider(4 * 60);  // greeting boundaries, as in greeting()
  consider(12 * 60);
  consider(18 * 60);
  for (const auto& l : set.lists) {
    if (l.anyTime()) continue;
    consider(l.fromMin);
    consider(l.toMin);
  }
  return best;
}

std::string formatHhmm(int minuteOfDay) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
  return buf;
}

int parseHhmm(const std::string& text) {
  if (text.size() != 5 || text[2] != ':') return -1;
  for (int i : {0, 1, 3, 4}) {
    if (text[i] < '0' || text[i] > '9') return -1;
  }
  const int h = (text[0] - '0') * 10 + (text[1] - '0');
  const int m = (text[3] - '0') * 10 + (text[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

}  // namespace core
