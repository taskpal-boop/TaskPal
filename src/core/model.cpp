#include "model.h"

#include <algorithm>
#include <utility>

#include "catalog.h"
#include "text_rules.h"

namespace core {

const char* celebrationKey(Celebration c) {
  switch (c) {
    case Celebration::Corgi: return "corgi";
    case Celebration::Poop: return "poop";
    default: return "star";
  }
}

Celebration celebrationFromKey(const std::string& key) {
  if (key == "corgi") return Celebration::Corgi;
  if (key == "poop") return Celebration::Poop;
  return Celebration::Star;
}

namespace {

TaskList makeList(ListSet& set, const char* name, const char* icon, int from, int to,
                  std::initializer_list<std::pair<const char*, const char*>> tasks) {
  TaskList l;
  l.id = set.nextId++;
  l.name = name;
  l.icon = icon;
  l.fromMin = from;
  l.toMin = to;
  for (const auto& [label, taskIcon] : tasks) l.tasks.push_back({set.nextId++, label, taskIcon});
  return l;
}

Task* findTask(TaskList& list, uint32_t taskId) {
  for (auto& t : list.tasks) {
    if (t.id == taskId) return &t;
  }
  return nullptr;
}

template <typename T, typename Pred>
Result moveIn(std::vector<T>& items, Pred matches, int delta) {
  auto it = std::find_if(items.begin(), items.end(), matches);
  if (it == items.end()) return Result::NotFound;
  const long from = it - items.begin();
  const long to = from + delta;
  if (delta == 0 || to < 0 || to >= long(items.size())) return Result::Invalid;
  std::swap(items[from], items[to]);
  return Result::Ok;
}

}  // namespace

ListSet defaultLists() {
  ListSet set;
  set.lists.push_back(makeList(set, "Morning", "sun", 6 * 60 + 30, 8 * 60,
                               {{"Get dressed", "shirt"}, {"Brush teeth", "tooth"}, {"Eat breakfast", "bowl"},
                                {"Wash face", "drop"}, {"Comb hair", "comb"}, {"Pack backpack", "backpack"}}));
  set.lists.push_back(makeList(set, "After school", "house", 15 * 60, 18 * 60,
                               {{"Homework", "pencil"}, {"Tidy toys", "box"}, {"Practice music", "music"},
                                {"Drink water", "glass"}}));
  set.lists.push_back(makeList(set, "Bedtime", "moon", 19 * 60, 20 * 60 + 30,
                               {{"Pajamas on", "shirt"}, {"Brush teeth", "tooth"}, {"Drink water", "glass"},
                                {"Read", "book"}, {"Quiet time", "clock"}}));
  return set;
}

TaskList* findList(ListSet& set, uint32_t listId) {
  for (auto& l : set.lists) {
    if (l.id == listId) return &l;
  }
  return nullptr;
}

const TaskList* findList(const ListSet& set, uint32_t listId) {
  return findList(const_cast<ListSet&>(set), listId);
}

bool isValidLabel(const std::string& text) {
  const std::string t = trimmed(text);
  return !t.empty() && t.size() <= kMaxLabelLen;
}

Result addList(ListSet& set, const std::string& name, const std::string& icon, uint32_t* newId) {
  if (set.lists.size() >= kMaxLists) return Result::Full;
  if (!isValidLabel(name) || !isListIcon(icon)) return Result::Invalid;
  TaskList l;
  l.id = set.nextId++;
  l.name = trimmed(name);
  l.icon = icon;
  set.lists.push_back(l);
  if (newId) *newId = l.id;
  return Result::Ok;
}

Result removeList(ListSet& set, Progress& progress, uint32_t listId) {
  auto it = std::find_if(set.lists.begin(), set.lists.end(), [&](const TaskList& l) { return l.id == listId; });
  if (it == set.lists.end()) return Result::NotFound;
  set.lists.erase(it);
  progress.done.erase(listId);  // stars already earned today are kept
  return Result::Ok;
}

Result moveList(ListSet& set, uint32_t listId, int delta) {
  return moveIn(set.lists, [&](const TaskList& l) { return l.id == listId; }, delta);
}

Result addTask(ListSet& set, uint32_t listId, const std::string& label, const std::string& icon,
               uint32_t* newId) {
  TaskList* list = findList(set, listId);
  if (!list) return Result::NotFound;
  if (list->tasks.size() >= kMaxTasks) return Result::Full;
  if (!isValidLabel(label) || !isTaskIcon(icon)) return Result::Invalid;
  Task t{set.nextId++, trimmed(label), icon};
  list->tasks.push_back(t);
  if (newId) *newId = t.id;
  return Result::Ok;
}

Result removeTask(ListSet& set, Progress& progress, uint32_t listId, uint32_t taskId) {
  TaskList* list = findList(set, listId);
  if (!list) return Result::NotFound;
  auto it = std::find_if(list->tasks.begin(), list->tasks.end(), [&](const Task& t) { return t.id == taskId; });
  if (it == list->tasks.end()) return Result::NotFound;
  list->tasks.erase(it);
  if (auto d = progress.done.find(listId); d != progress.done.end()) d->second.erase(taskId);  // star kept
  return Result::Ok;
}

Result moveTask(ListSet& set, uint32_t listId, uint32_t taskId, int delta) {
  TaskList* list = findList(set, listId);
  if (!list) return Result::NotFound;
  return moveIn(list->tasks, [&](const Task& t) { return t.id == taskId; }, delta);
}

Result renameTask(ListSet& set, uint32_t listId, uint32_t taskId, const std::string& label) {
  TaskList* list = findList(set, listId);
  Task* t = list ? findTask(*list, taskId) : nullptr;
  if (!t) return Result::NotFound;
  if (!isValidLabel(label)) return Result::Invalid;
  t->label = trimmed(label);
  return Result::Ok;
}

Result setTaskIcon(ListSet& set, uint32_t listId, uint32_t taskId, const std::string& icon) {
  TaskList* list = findList(set, listId);
  Task* t = list ? findTask(*list, taskId) : nullptr;
  if (!t) return Result::NotFound;
  if (!isTaskIcon(icon)) return Result::Invalid;
  t->icon = icon;
  return Result::Ok;
}

}  // namespace core
