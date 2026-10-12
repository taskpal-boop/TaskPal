#include "app.h"

#include <Arduino.h>
#include <esp_log.h>

#include <string>

#include "../core/json_codec.h"
#include "../core/logic_day.h"
#include "../core/stars.h"
#include "../hal/clock.h"
#include "../hal/net.h"
#include "../hal/storage.h"

namespace app {

namespace {
constexpr const char* kSettingsFile = "/settings.json";
constexpr const char* kListsFile = "/lists.json";
constexpr const char* kProgressFile = "/progress.json";
}  // namespace

State& state() {
  static State s;
  return s;
}

void load() {
  auto& s = state();
  std::string text;
  // "ok" = file read and decoded; "missing" = first boot; "BAD" = present but unreadable (PWR-06).
  auto status = [&](const char* path, bool decoded) {
    return decoded ? "ok" : (hal::storage::exists(path) ? "BAD" : "missing");
  };
  const bool settingsOk = hal::storage::read(kSettingsFile, text) && core::decodeSettings(text, s.settings);
  const char* settingsSt = status(kSettingsFile, settingsOk);
  if (!settingsOk) {
    s.settings = core::Settings{};
    saveSettings();
  }
  const bool listsOk = hal::storage::read(kListsFile, text) && core::decodeLists(text, s.lists);
  const char* listsSt = status(kListsFile, listsOk);
  if (!listsOk) {
    s.lists = core::defaultLists();
    saveLists();
  }
  const bool progressOk = hal::storage::read(kProgressFile, text) && core::decodeProgress(text, s.progress);
  const char* progressSt = status(kProgressFile, progressOk);
  if (!progressOk) s.progress = core::Progress{};
  hal::clock::setTz(s.settings.tz);
  // A lost clock (battery ran flat, D28) reads as 1999: keep the saved day until the time is set.
  if (hal::clock::valid() && core::rollover(s.progress, today(), s.settings.weekStartDow)) saveProgress();
  Serial.printf("[app] files: settings %s, lists %s, progress %s (%u lists, day %s)\n", settingsSt, listsSt,
                progressSt, unsigned(s.lists.lists.size()), s.progress.day.c_str());
}

void stressSaves(int count) {
  // PWR-06: back-to-back progress saves so a reset lands mid-write. Toggles the first task of the
  // first list each time and leaves it as it was when count is even.
  auto& s = state();
  if (s.lists.lists.empty() || s.lists.lists[0].tasks.empty()) return;
  const uint32_t listId = s.lists.lists[0].id, taskId = s.lists.lists[0].tasks[0].id;
  for (int i = 0; i < count; ++i) toggleTask(listId, taskId);
}

bool saveSettings() { return hal::storage::writeAtomic(kSettingsFile, core::encodeSettings(state().settings)); }
bool saveLists() { return hal::storage::writeAtomic(kListsFile, core::encodeLists(state().lists)); }
bool saveProgress() { return hal::storage::writeAtomic(kProgressFile, core::encodeProgress(state().progress)); }

core::LocalTime now() { return hal::clock::now(); }

core::Date today() { return core::logicalDay(now(), state().settings.resetHour); }

bool rolloverIfNeeded(bool allowSync) {
  auto& s = state();
  if (!hal::clock::valid()) return false;  // no real date yet (D28)
  if (!core::rollover(s.progress, today(), s.settings.weekStartDow)) return false;
  saveProgress();
  // New day (normally at the reset hour): re-sync the clock over WiFi if set up (M7).
  if (allowSync && s.settings.autoSync && hal::net::hasCredentials()) {
    const auto r = hal::net::syncSaved(s.settings.tz);
    ESP_LOGI("app", "daily time sync: %s", hal::net::describe(r));
    if (r == hal::net::SyncResult::Ok) {
      s.settings.lastSync = hal::clock::epoch();
      saveSettings();
      // The corrected clock may be a different logical day than the RTC said: settle it now, without
      // another sync (the next periodic check synced twice after a big correction, M8 test).
      if (core::rollover(s.progress, today(), s.settings.weekStartDow)) saveProgress();
    }
  }
  return true;
}

void toggleTask(uint32_t listId, uint32_t taskId) {
  auto& p = state().progress;
  if (core::isDone(p, listId, taskId)) {
    core::uncheck(p, listId, taskId);
  } else {
    core::check(p, listId, taskId);
  }
  saveProgress();
}

void restartList(uint32_t listId) {
  core::restartList(state().progress, listId);
  saveProgress();
}

bool listComplete(const core::TaskList& list) {
  return !list.tasks.empty() && core::doneCount(state().progress, list) == list.tasks.size();
}

int weeklyStars() {
  const auto& s = state();
  return core::weeklyStars(s.progress, today(), s.settings.weekStartDow);
}

}  // namespace app
