#include "stars.h"

#include "logic_day.h"

namespace core {

bool rollover(Progress& progress, const Date& today, int weekStartDow) {
  const std::string iso = toIso(today);
  if (progress.day == iso) return false;
  progress.day = iso;
  progress.done.clear();  // yesterday's checks go; the stars they earned stay
  const std::string first = toIso(weekStart(today, weekStartDow));
  for (auto it = progress.stars.begin(); it != progress.stars.end();) {
    it = it->first < first ? progress.stars.erase(it) : std::next(it);
  }
  return true;
}

bool isDone(const Progress& progress, uint32_t listId, uint32_t taskId) {
  auto it = progress.done.find(listId);
  return it != progress.done.end() && it->second.count(taskId) > 0;
}

size_t doneCount(const Progress& progress, const TaskList& list) {
  size_t n = 0;
  for (const auto& t : list.tasks) n += isDone(progress, list.id, t.id) ? 1 : 0;
  return n;
}

namespace {
void takeStars(Progress& progress, size_t n) {
  auto it = progress.stars.find(progress.day);
  if (it == progress.stars.end()) return;
  it->second = n >= size_t(it->second) ? 0 : it->second - int(n);
  if (it->second == 0) progress.stars.erase(it);
}
}  // namespace

void check(Progress& progress, uint32_t listId, uint32_t taskId) {
  if (progress.done[listId].insert(taskId).second) ++progress.stars[progress.day];
}

void uncheck(Progress& progress, uint32_t listId, uint32_t taskId) {
  auto d = progress.done.find(listId);
  if (d == progress.done.end() || d->second.erase(taskId) == 0) return;  // was not checked
  takeStars(progress, 1);
}

void restartList(Progress& progress, uint32_t listId) {
  auto d = progress.done.find(listId);
  if (d == progress.done.end()) return;
  takeStars(progress, d->second.size());  // every task is unchecked, so its star goes too (D25)
  progress.done.erase(d);
}

int starsToday(const Progress& progress) {
  auto it = progress.stars.find(progress.day);
  return it == progress.stars.end() ? 0 : it->second;
}

int weeklyStars(const Progress& progress, const Date& today, int weekStartDow) {
  const std::string first = toIso(weekStart(today, weekStartDow));
  const std::string last = toIso(today);
  int total = 0;
  for (const auto& [day, n] : progress.stars) {
    if (day >= first && day <= last) total += n;
  }
  return total;
}

}  // namespace core
