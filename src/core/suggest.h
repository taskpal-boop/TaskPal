// Keyboard suggestions and the "New task" sheet logic (DESIGN_SPEC §7.4, §7.5).
#pragma once
#include <string>
#include <vector>

namespace core {

// Case-insensitive prefix matches from the phrase list, excluding an exact match, at most `max`.
// An empty (or all-space) input returns the three default suggestions.
std::vector<std::string> suggestions(const std::string& typed, size_t max = 3);

// State of the New-task sheet.
struct NewTaskDraft {
  std::string text;
  std::string icon;      // empty until a picture is picked
  bool autoLabel = true; // text came from a picture (or is empty), so a new picture may replace it
};

void pickIcon(NewTaskDraft& draft, const std::string& icon);
void setTextByUser(NewTaskDraft& draft, const std::string& text);  // typing or a suggestion tap

struct AddButton {
  bool enabled = false;
  std::string label;  // "Pick a picture" / "Type a task name" / Add “Feed the cat”
};
AddButton addButton(const NewTaskDraft& draft);

}  // namespace core
