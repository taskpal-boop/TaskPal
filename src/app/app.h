// TaskPal runtime state: settings, lists and today's progress, persisted as JSON in LittleFS
// through core/json_codec (ARCHITECTURE §7). One instance, owned here.
#pragma once
#include <cstdint>

#include "../core/date.h"
#include "../core/model.h"

namespace app {

struct State {
  core::Settings settings;
  core::ListSet lists;
  core::Progress progress;
};

State& state();

// Loads the three files (writing factory defaults for any that are missing or unreadable)
// and rolls progress over to today's logical day.
void load();
bool saveSettings();
bool saveLists();
bool saveProgress();

core::LocalTime now();
core::Date today();  // logical day (reset hour applied)

// Moves progress to a new logical day. Returns true if the day changed. allowSync = also re-sync the
// clock over WiFi (only for the natural day change from the periodic checks, not after the clock was
// set by hand).
bool rolloverIfNeeded(bool allowSync = false);

// Kid actions (save progress immediately).
void toggleTask(uint32_t listId, uint32_t taskId);  // check or uncheck
void restartList(uint32_t listId);
bool listComplete(const core::TaskList& list);
int weeklyStars();

// Test hook (serial 'W', PWR-06): count back-to-back progress saves.
void stressSaves(int count);

}  // namespace app
