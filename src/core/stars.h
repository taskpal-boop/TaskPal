// Checking tasks, day/week rollover and star counting (D8: one star per task checked).
#pragma once
#include "date.h"
#include "model.h"

namespace core {

// Moves progress to `today` (a logical day). On a new day, clears the checks (the stars they
// earned are kept) and drops star entries from before the current week. Returns true if the day changed.
bool rollover(Progress& progress, const Date& today, int weekStartDow);

bool isDone(const Progress& progress, uint32_t listId, uint32_t taskId);
size_t doneCount(const Progress& progress, const TaskList& list);  // counts only tasks still in the list

// Check / uncheck on progress.day (D25). Every task checked today holds one of today's stars:
// unchecking it, or "Start this list again", takes the star back, even after the list was
// completed. Earlier days' stars can't be taken back (their checks are gone after rollover).
void check(Progress& progress, uint32_t listId, uint32_t taskId);
void uncheck(Progress& progress, uint32_t listId, uint32_t taskId);
void restartList(Progress& progress, uint32_t listId);

int starsToday(const Progress& progress);
int weeklyStars(const Progress& progress, const Date& today, int weekStartDow);

}  // namespace core
