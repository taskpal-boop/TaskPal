// On-screen keyboard typing rules (DESIGN_SPEC §7.5). ASCII only: the keyboard has no accents.
#pragma once
#include <cstddef>
#include <string>

namespace core {

enum class KeyResult {
  Added,    // text changed
  Ignored,  // no change, no feedback (e.g. a leading space)
  AtLimit,  // no change; the key flashes to show the limit
};

struct TextField {
  std::string text;
  size_t maxLen = 18;
  bool shift = false;      // Shift toggle: stays on until tapped again (D22)
  bool autoCap = true;     // capitalise the first letter (off for passwords)
  bool freeSpaces = false; // allow leading/double spaces (passwords)
};

KeyResult typeChar(TextField& field, char c);  // letters, digits, punctuation (not space)
KeyResult typeSpace(TextField& field);
bool backspace(TextField& field);

std::string trimmed(const std::string& text);

}  // namespace core
