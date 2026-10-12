#include "catalog.h"

namespace core {

namespace {
constexpr IconGroup E = IconGroup::Everyday, H = IconGroup::Home;
}  // namespace

// Tile captions come from the Change picture artboard (D26).
const IconInfo kTaskIcons[kTaskIconCount] = {
    {"shirt", "Get dressed", "Get dressed", E},     {"tooth", "Brush teeth", "Brush teeth", E},
    {"bowl", "Eat breakfast", "Breakfast", E},      {"drop", "Wash face", "Wash face", E},
    {"comb", "Comb hair", "Comb hair", E},          {"backpack", "Pack backpack", "Backpack", E},
    {"shoe", "Put on shoes", "Shoes", E},           {"bed", "Make bed", "Make bed", E},
    {"book", "Read", "Read", E},                    {"pencil", "Homework", "Homework", E},
    {"box", "Tidy toys", "Tidy toys", E},           {"glass", "Drink water", "Drink water", E},
    {"plant", "Water plant", "Water plant", E},     {"clock", "Quiet time", "Quiet time", E},
    {"music", "Practice music", "Music", E},        {"star", "Free choice", "Free choice", E},
    {"table", "Set the table", "Set the table", H}, {"meal", "Eat dinner", "Eat dinner", H},
    {"toilet", "Use the toilet", "Use the toilet", H}, {"hands", "Wash hands", "Wash hands", H},
    {"tv", "TV time", "TV time", H},                {"toys", "Play with toys", "Play with toys", H},
    {"broom", "Clean up", "Clean up", H},           {"laundry", "Laundry basket", "Laundry basket", H},
};

const IconInfo kListIcons[kListIconCount] = {
    {"sun", "Morning"}, {"house", "Home"}, {"moon", "Night"}, {"book", "School"}, {"star", "Special"},
};

const char* groupName(IconGroup g) { return g == IconGroup::Home ? "Home & chores" : "Everyday"; }

bool isTaskIcon(const std::string& key) { return taskIconIndex(key) >= 0; }

int taskIconIndex(const std::string& key) {
  for (int i = 0; i < kTaskIconCount; ++i) {
    if (key == kTaskIcons[i].key) return i;
  }
  return -1;
}

bool isListIcon(const std::string& key) {
  for (const auto& i : kListIcons) {
    if (key == i.key) return true;
  }
  return false;
}

std::string suggestedLabel(const std::string& taskIcon) {
  const int i = taskIconIndex(taskIcon);
  return i < 0 ? std::string() : kTaskIcons[i].label;
}

}  // namespace core
