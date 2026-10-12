// Which list gets the "NOW" pill, and the Pick-a-list greeting (DESIGN_SPEC §5.1).
#pragma once
#include <string>

#include "model.h"

namespace core {

// True if minuteOfDay is inside [from, to). Windows may cross midnight (from > to).
bool inWindow(int fromMin, int toMin, int minuteOfDay);

// Index into set.lists of the first list whose window contains the time; -1 if none.
// "Any time" lists never get the NOW pill.
int nowListIndex(const ListSet& set, int minuteOfDay);

// 04:00-11:59 "Good morning", 12:00-17:59 "Good afternoon", otherwise "Good evening";
// ", Sam" is added when a child's name is set.
std::string greeting(int hour, const std::string& childName);

// Minutes from minuteOfDay to the next moment Pick a list would look different (D30): a list
// window starting or ending, or the greeting changing at 04:00 / 12:00 / 18:00. Always 1..1440.
int minutesToNextChange(const ListSet& set, int minuteOfDay);

// "06:30" <-> 390 minutes. parseHhmm returns -1 on bad input.
std::string formatHhmm(int minuteOfDay);
int parseHhmm(const std::string& text);

}  // namespace core
