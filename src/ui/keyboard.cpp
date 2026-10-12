#include "keyboard.h"

#include <algorithm>

#include "fonts.h"
#include "icons.h"
#include "region.h"
#include "theme.h"

namespace ui {

using namespace theme;
using hal::Rect;

namespace {
constexpr int kSuggestY = Keyboard::kTop + 4 + 10;  // 606
constexpr int kRowY[4] = {666, 738, 810, 882};
constexpr int kKeyH = 64;
constexpr int kGap = 6;
const char* const kLetters[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* const kNumbers[3] = {"1234567890", "-/:()&@+", ".,?!'"};
// Password symbols (M7): [ ] { } # % ^ * + =   _ \ | ~ < > $ " ;
const char* const kSymbols[3] = {"[]{}#%^*+=", "_\\|~<>$\";", ".,?!'"};
}  // namespace

Keyboard::Keyboard(bool symbols) : symbols_(symbols) { layout(); }

void Keyboard::layout() {
  keys_.clear();
  using T = Action::Type;
  const char* const* rows = layer_ == 0 ? kLetters : (layer_ == 1 ? kNumbers : kSymbols);
  // Row 1: 10 keys, 47 wide.
  for (int i = 0; rows[0][i]; ++i) {
    keys_.push_back({Rect{int16_t(8 + i * (47 + kGap)), kRowY[0], 47, kKeyH}, T::Char, rows[0][i], nullptr});
  }
  // Row 2: 9 letters (48 wide, inset 24) or 8-9 symbols (centred).
  const int n2 = int(strlen(rows[1]));
  const int x2 = 6 + (528 - (n2 * 48 + (n2 - 1) * kGap)) / 2;
  for (int i = 0; i < n2; ++i) {
    keys_.push_back({Rect{int16_t(x2 + i * (48 + kGap)), kRowY[1], 48, kKeyH}, T::Char, rows[1][i], nullptr});
  }
  // Row 3: Shift (letters) or #+= / 123 (symbol-enabled keyboards), keys, Delete.
  if (layer_ == 0) {
    keys_.push_back({Rect{6, kRowY[2], 72, kKeyH}, T::Shift, 0, "shift"});
  } else if (symbols_) {
    keys_.push_back({Rect{6, kRowY[2], 72, kKeyH}, T::Symbols, 0, layer_ == 1 ? "#+=" : "123"});
  }
  const int n3 = int(strlen(rows[2]));
  for (int i = 0; i < n3; ++i) {
    keys_.push_back({Rect{int16_t(84 + i * (48 + kGap)), kRowY[2], 48, kKeyH}, T::Char, rows[2][i], nullptr});
  }
  keys_.push_back({Rect{462, kRowY[2], 72, kKeyH}, T::Backspace, 0, "del"});
  // Row 4: 123/ABC, space, Done.
  keys_.push_back({Rect{6, kRowY[3], 96, kKeyH}, T::Layer, 0, layer_ == 0 ? "123" : "ABC"});
  keys_.push_back({Rect{108, kRowY[3], 300, kKeyH}, T::Space, ' ', "space"});
  keys_.push_back({Rect{414, kRowY[3], 120, kKeyH}, T::Done, 0, "Done"});
}

void Keyboard::drawKey(Gfx& g, const Key& k, int ox, int oy, bool pressed) const {
  using T = Action::Type;
  const int x = k.r.x + ox, y = k.r.y + oy, w = k.r.w, h = k.r.h;
  const bool modifier = k.type == T::Shift || k.type == T::Backspace || k.type == T::Layer || k.type == T::Symbols;
  const bool dark = pressed || k.type == T::Done || (k.type == T::Shift && shift_);
  g.fillRoundRect(x, y, w, h, 10, kPureBlack);
  if (!dark) {
    if (modifier) {
      g.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 8, kPureWhite);
      halftoneRoundRect(g, x + 2, y + 2, w - 4, h - 4, 8, kDivider, -ox, -oy);
    } else {
      g.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 8, kPureWhite);
    }
  }
  const int ink = dark ? kWhite : kInk;
  const int bg = dark ? kInk : kWhite;
  switch (k.type) {
    case T::Shift: drawIcon(g, "ui_shift", 32, x + (w - 32) / 2, y + (h - 32) / 2, ink, bg, true); break;
    case T::Backspace: drawIcon(g, "ui_backspace", 32, x + (w - 32) / 2, y + (h - 32) / 2, ink, bg, true); break;
    case T::Layer:
    case T::Symbols: drawText(g, fonts::Id::atkinson_20b, k.label, x + w / 2, y + h / 2, lgfx::middle_center, ink); break;
    case T::Space: drawText(g, fonts::Id::atkinson_20b, "space", x + w / 2, y + h / 2, lgfx::middle_center, ink); break;
    case T::Done: drawText(g, fonts::Id::atkinson_22b, "Done", x + w / 2, y + h / 2, lgfx::middle_center, ink); break;
    default: {
      const char c = (upper_ && k.c >= 'a' && k.c <= 'z') ? char(k.c - 'a' + 'A') : k.c;
      drawText(g, fonts::Id::atkinson_26b, std::string(1, c), x + w / 2, y + h / 2 - 2, lgfx::middle_center, ink);
      break;
    }
  }
}

void Keyboard::drawSuggestionRow(Gfx& g, int ox, int oy) const {
  const int y = kSuggestY + oy;
  const int w = (528 - 2 * kGap) / 3;
  for (size_t i = 0; i < suggestions_.size() && i < 3; ++i) {
    const int x = 6 + int(i) * (w + kGap) + ox;
    const bool pressed = int(i) == downSuggestion_;
    g.fillRoundRect(x, y, w, 52, 12, kPureBlack);
    if (!pressed) g.fillRoundRect(x + 2, y + 2, w - 4, 48, 10, kPureWhite);
    drawText(g, fonts::Id::atkinson_18b, suggestions_[i], x + w / 2, y + 26, lgfx::middle_center,
             pressed ? kWhite : kInk);
  }
}

void Keyboard::draw(Gfx& g, int ox, int oy) const {
  const int x = kRect.x + ox, y = kRect.y + oy;
  g.fillRect(x, y, kRect.w, kRect.h, kPureWhite);
  halftoneRoundRect(g, x, y + 4, kRect.w, kRect.h - 4, 0, kDone, -ox, -oy);
  g.fillRect(x, y, kRect.w, 4, kPureBlack);
  // Clear the suggestion strip to the tray pattern, then draw the buttons.
  drawSuggestionRow(g, ox, oy);
  for (const auto& k : keys_) drawKey(g, k, ox, oy, false);
}

void Keyboard::redraw() const {
  Region r(kRect);
  draw(r.canvas(), -kRect.x, -kRect.y);
  r.binarize();
  r.push(hal::Mode::Fast);
}

void Keyboard::pushKey(const Key& k, bool pressed, hal::Mode m) const {
  Region r(k.r);
  // Tray pattern behind the rounded corners.
  halftoneRoundRect(r.canvas(), 0, 0, k.r.w, k.r.h, 0, kDone, k.r.x, k.r.y);
  drawKey(r.canvas(), k, -k.r.x, -k.r.y, pressed);
  r.binarize();
  r.push(m);
}

void Keyboard::setSuggestions(const std::vector<std::string>& s) {
  if (s == suggestions_) return;
  suggestions_ = s;
  restoreSuggestions_ = true;  // redrawn in tick() when the panel is free
}

void Keyboard::setCaps(bool shift, bool upper, bool redraw) {
  const bool caseChanged = upper != upper_;
  const bool shiftChanged = shift != shift_;
  shift_ = shift;
  upper_ = upper;
  if (!redraw) return;
  if (caseChanged && layer_ == 0) {
    redrawAll_ = true;  // every letter key changes
  } else if (shiftChanged) {
    for (size_t i = 0; i < keys_.size(); ++i) {
      if (keys_[i].type == Action::Type::Shift) toRestore_.push_back(int(i));
    }
  }
}

const Keyboard::Key* Keyboard::keyAt(int x, int y) const {
  for (const auto& k : keys_) {
    // Hit areas are 3 px larger than the drawn keys, so the gaps count too.
    const Rect hit{int16_t(k.r.x - 3), int16_t(k.r.y - 4), int16_t(k.r.w + 6), int16_t(k.r.h + 8)};
    if (hit.contains(x, y)) return &k;
  }
  return nullptr;
}

int Keyboard::suggestionAt(int x, int y) const {
  const int w = (528 - 2 * kGap) / 3;
  if (y < kSuggestY || y >= kSuggestY + 52) return -1;
  for (size_t i = 0; i < suggestions_.size() && i < 3; ++i) {
    const int sx = 6 + int(i) * (w + kGap);
    if (x >= sx && x < sx + w) return int(i);
  }
  return -1;
}

Keyboard::Action Keyboard::onTouch(const hal::TouchEvent& e) {
  using T = hal::TouchEvent::Type;
  Action a;
  if (!kRect.contains(e.x, e.y)) return a;
  switch (e.type) {
    case T::Down: {
      downSuggestion_ = suggestionAt(e.x, e.y);
      const Key* k = downSuggestion_ < 0 ? keyAt(e.x, e.y) : nullptr;
      downKey_ = k ? int(k - keys_.data()) : -1;
      if (k) pushKey(*k, true, hal::Mode::Fastest);
      if (downSuggestion_ >= 0) {
        Region r(Rect{0, kSuggestY, 540, 52});
        halftoneRoundRect(r.canvas(), 0, 0, 540, 52, 0, kDone, 0, kSuggestY);
        drawSuggestionRow(r.canvas(), 0, -kSuggestY);
        r.binarize();
        r.push(hal::Mode::Fastest);
      }
      break;
    }
    case T::Tap: {
      if (downSuggestion_ >= 0) {
        a.type = Action::Type::Suggestion;
        a.text = suggestions_[downSuggestion_];
        downSuggestion_ = -1;
        restoreSuggestions_ = true;
      } else if (downKey_ >= 0) {
        const Key k = keys_[downKey_];
        a.type = k.type;
        a.c = k.c;
        toRestore_.push_back(downKey_);
        downKey_ = -1;
        if (k.type == Action::Type::Layer || k.type == Action::Type::Symbols) {
          if (k.type == Action::Type::Layer) layer_ = layer_ == 0 ? 1 : 0;
          else layer_ = layer_ == 1 ? 2 : 1;
          layout();
          toRestore_.clear();
          redraw();
        }
      }
      break;
    }
    case T::Up:
      if (downKey_ >= 0) toRestore_.push_back(downKey_);
      if (downSuggestion_ >= 0) restoreSuggestions_ = true;
      downKey_ = -1;
      downSuggestion_ = -1;
      break;
    default:
      break;
  }
  return a;
}

bool Keyboard::tick() {
  if (hal::epd().busy()) return !toRestore_.empty() || restoreSuggestions_ || redrawAll_;
  if (redrawAll_) {
    redrawAll_ = false;
    toRestore_.clear();
    restoreSuggestions_ = false;
    redraw();
    return true;
  }
  if (restoreSuggestions_) {
    restoreSuggestions_ = false;
    Region r(Rect{0, kSuggestY, 540, 52});
    halftoneRoundRect(r.canvas(), 0, 0, 540, 52, 0, kDone, 0, kSuggestY);
    drawSuggestionRow(r.canvas(), 0, -kSuggestY);
    r.binarize();
    r.push(hal::Mode::Fast);
    return true;
  }
  if (!toRestore_.empty()) {
    const int i = toRestore_.front();
    toRestore_.erase(toRestore_.begin());
    if (i < int(keys_.size())) pushKey(keys_[i], false, hal::Mode::Fast);
    return true;
  }
  return false;
}

}  // namespace ui
