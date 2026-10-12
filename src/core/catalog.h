// Picture catalog from the mockups (DESIGN_SPEC §3). Keys match assets/icons/*.svg and lists.json.
#pragma once
#include <string>

namespace core {

// Task picture groups: the tabs of the Change picture screen (D26).
enum class IconGroup { Everyday, Home };
constexpr int kIconGroupCount = 2;
const char* groupName(IconGroup g);  // "Everyday", "Home & chores"

struct IconInfo {
  const char* key;
  const char* label;  // suggested task label / list name
  const char* tile = nullptr;  // short caption on picture tiles (task icons only)
  IconGroup group = IconGroup::Everyday;
};

constexpr int kTaskIconCount = 24;  // 16 Everyday, then 8 Home & chores
constexpr int kListIconCount = 5;
extern const IconInfo kTaskIcons[kTaskIconCount];
extern const IconInfo kListIcons[kListIconCount];

bool isTaskIcon(const std::string& key);
bool isListIcon(const std::string& key);
int taskIconIndex(const std::string& key);  // index into kTaskIcons, or -1

// "tooth" -> "Brush teeth"; empty string if unknown.
std::string suggestedLabel(const std::string& taskIcon);

}  // namespace core
