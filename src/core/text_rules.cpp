#include "text_rules.h"

namespace core {

namespace {
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
char upper(char c) { return isLower(c) ? char(c - 'a' + 'A') : c; }
}  // namespace

KeyResult typeChar(TextField& field, char c) {
  if (c == ' ') return typeSpace(field);
  if (field.text.size() >= field.maxLen) return KeyResult::AtLimit;
  // Auto-capitalise the first letter; Shift is a toggle (stays on until tapped again, D22).
  if ((field.autoCap && field.text.empty()) || field.shift) c = upper(c);
  field.text.push_back(c);
  return KeyResult::Added;
}

KeyResult typeSpace(TextField& field) {
  if (field.text.size() >= field.maxLen) return KeyResult::Ignored;
  if (!field.freeSpaces && (field.text.empty() || field.text.back() == ' ')) return KeyResult::Ignored;
  field.text.push_back(' ');
  return KeyResult::Added;
}

bool backspace(TextField& field) {
  if (field.text.empty()) return false;
  field.text.pop_back();
  return true;
}

std::string trimmed(const std::string& text) {
  const auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(' ');
  return text.substr(first, last - first + 1);
}

}  // namespace core
