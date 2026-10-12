// TaskPal data model: settings, task lists and today's progress (ARCHITECTURE §7).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace core {

constexpr size_t kMaxLists = 6;
constexpr size_t kMaxTasks = 8;
constexpr size_t kMaxLabelLen = 18;  // task labels and list names ("short words read best")
constexpr int kSchemaVersion = 1;

struct Settings {
  std::string childName;
  int resetHour = 4;
  bool weeklyStars = true;
  int sleepMin = 2;
  std::string tz = "EST5EDT,M3.2.0,M11.1.0";  // POSIX TZ, US Eastern (D23)
  int weekStartDow = 1;     // 1 = Monday
  std::string pinSalt;      // hex; empty = no PIN yet (created on first admin entry)
  std::string pinHash;      // hex SHA-256(salt || pin)
  int64_t lastSync = 0;     // epoch seconds of the last NTP sync
  bool autoSync = true;     // re-sync the clock over WiFi once a day at the reset hour (M7)
  bool autoRefresh = true;  // asleep: wake when Pick a list would change (NOW, greeting) and redraw (D30)
};

struct Task {
  uint32_t id = 0;
  std::string label;
  std::string icon;
};

// The All done screen a list shows (D27): the original star, or a dancing corgi or poop.
enum class Celebration : uint8_t { Star, Corgi, Poop };
constexpr int kCelebrationCount = 3;
const char* celebrationKey(Celebration c);          // "star" / "corgi" / "poop" (lists.json)
Celebration celebrationFromKey(const std::string& key);  // unknown -> Star

struct TaskList {
  uint32_t id = 0;
  std::string name;
  std::string icon;
  int fromMin = -1;  // time window in minutes since midnight; -1 = "Any time"
  int toMin = -1;
  Celebration celebration = Celebration::Star;
  std::vector<Task> tasks;

  bool anyTime() const { return fromMin < 0 || toMin < 0; }
};

struct ListSet {
  uint32_t nextId = 1;  // ids are never reused
  std::vector<TaskList> lists;
};

// Progress for the current logical day, plus star history for the current week.
struct Progress {
  std::string day;                                   // logical day, "YYYY-MM-DD"
  std::map<uint32_t, std::set<uint32_t>> done;       // listId -> checked taskIds (one star each, D25)
  std::map<std::string, int> stars;                  // "YYYY-MM-DD" -> stars earned that day
};

enum class Result { Ok, NotFound, Full, Invalid };

// Factory defaults: Morning / After school / Bedtime (DESIGN_SPEC §4).
ListSet defaultLists();

TaskList* findList(ListSet& set, uint32_t listId);
const TaskList* findList(const ListSet& set, uint32_t listId);

// Name/label: trimmed, 1..kMaxLabelLen characters.
bool isValidLabel(const std::string& text);

Result addList(ListSet& set, const std::string& name, const std::string& icon, uint32_t* newId = nullptr);
Result removeList(ListSet& set, Progress& progress, uint32_t listId);
Result moveList(ListSet& set, uint32_t listId, int delta);  // -1 = up, +1 = down

Result addTask(ListSet& set, uint32_t listId, const std::string& label, const std::string& icon,
               uint32_t* newId = nullptr);
Result removeTask(ListSet& set, Progress& progress, uint32_t listId, uint32_t taskId);
Result moveTask(ListSet& set, uint32_t listId, uint32_t taskId, int delta);
Result renameTask(ListSet& set, uint32_t listId, uint32_t taskId, const std::string& label);
Result setTaskIcon(ListSet& set, uint32_t listId, uint32_t taskId, const std::string& icon);

}  // namespace core
