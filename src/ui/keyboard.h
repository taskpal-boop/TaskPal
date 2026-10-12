// On-screen keyboard (DESIGN_SPEC §7.5): suggestion row + 4 key rows in the bottom 368 px.
// Drawn pure black/white (halftone grays, D18) so every update can use DU/DU4.
#pragma once
#include <string>
#include <vector>

#include "../hal/epd.h"
#include "../hal/touch.h"
#include "widgets.h"

namespace ui {

class Keyboard {
 public:
  static constexpr int kTop = 592;
  static constexpr int kHeight = 368;
  static constexpr hal::Rect kRect{0, kTop, 540, kHeight};

  struct Action {
    enum class Type { None, Char, Space, Backspace, Shift, Layer, Symbols, Done, Suggestion };
    Type type = Type::None;
    char c = 0;
    std::string text;  // suggestion
  };

  // symbols = offer the #+= layer (passwords, M7).
  explicit Keyboard(bool symbols = false);

  // Whole tray into `g` at (ox, oy) offset of the target (full canvas: 0, 0).
  void draw(Gfx& g, int ox, int oy) const;
  // Redraws the tray on the panel (layer or shift change).
  void redraw() const;

  void setSuggestions(const std::vector<std::string>& s);  // redraws the suggestion row if changed
  // shift = Shift key highlighted; upper = letter keys show capitals (Shift on, or the next letter
  // will be auto-capitalised). A case change redraws the tray from tick(); redraw=false for setup.
  void setCaps(bool shift, bool upper, bool redraw = true);
  bool numbers() const { return layer_ != 0; }

  // Touch-down inverts the key at once (DU4); the key is restored later in tick().
  Action onTouch(const hal::TouchEvent& e);
  // Restores released keys when the panel is idle. Returns true while work is pending.
  bool tick();

 private:
  struct Key {
    hal::Rect r;
    Action::Type type;
    char c;
    const char* label;
  };
  void layout();
  void drawKey(Gfx& g, const Key& k, int ox, int oy, bool pressed) const;
  void drawSuggestionRow(Gfx& g, int ox, int oy) const;
  void pushKey(const Key& k, bool pressed, hal::Mode m) const;
  const Key* keyAt(int x, int y) const;
  int suggestionAt(int x, int y) const;

  std::vector<Key> keys_;
  std::vector<std::string> suggestions_;
  int layer_ = 0;  // 0 letters, 1 numbers, 2 symbols
  bool symbols_ = false;
  bool shift_ = false;
  bool upper_ = false;
  bool redrawAll_ = false;
  int downKey_ = -1;      // index into keys_ while the finger is down
  int downSuggestion_ = -1;
  std::vector<int> toRestore_;
  bool restoreSuggestions_ = false;
};

}  // namespace ui
