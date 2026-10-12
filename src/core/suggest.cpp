#include "suggest.h"

#include "catalog.h"
#include "text_rules.h"

namespace core {

namespace {

const char* const kPhrases[] = {
    "Feed the cat", "Feed the dog",  "Floss",       "Fold clothes",   "Put on shoes",  "Pack lunch",
    "Pajamas on",   "Brush hair",    "Bath time",   "Read a book",    "Tidy room",     "Take vitamins",
    "Walk the dog", "Water plants",  "Wash hands",  "Homework",       "Set the table", "Make bed",
    "Clear plate",  "Get dressed",   "Practice piano", "Put toys away",
};
const char* const kDefaults[] = {"Feed the dog", "Tidy room", "Wash hands"};

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

bool startsWithNoCase(const std::string& s, const std::string& prefix) {
  if (prefix.size() > s.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (lower(s[i]) != lower(prefix[i])) return false;
  }
  return true;
}

}  // namespace

std::vector<std::string> suggestions(const std::string& typed, size_t max) {
  std::vector<std::string> out;
  if (trimmed(typed).empty()) {
    for (const char* d : kDefaults) {
      if (out.size() < max) out.push_back(d);
    }
    return out;
  }
  for (const char* p : kPhrases) {
    const std::string phrase = p;
    if (phrase.size() == typed.size() && startsWithNoCase(phrase, typed)) continue;  // exact match
    if (startsWithNoCase(phrase, typed) && out.size() < max) out.push_back(phrase);
  }
  return out;
}

void pickIcon(NewTaskDraft& draft, const std::string& icon) {
  draft.icon = icon;
  if (draft.autoLabel || trimmed(draft.text).empty()) {
    draft.text = suggestedLabel(icon);
    draft.autoLabel = true;
  }
}

void setTextByUser(NewTaskDraft& draft, const std::string& text) {
  draft.text = text;
  draft.autoLabel = text.empty();  // clearing the field turns auto-fill back on
}

AddButton addButton(const NewTaskDraft& draft) {
  if (draft.icon.empty()) return {false, "Pick a picture"};
  const std::string name = trimmed(draft.text);
  if (name.empty()) return {false, "Type a task name"};
  return {true, "Add “" + name + "”"};
}

}  // namespace core
