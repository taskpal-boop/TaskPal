// Parent-area controls (DESIGN_SPEC §7): small icon buttons, toggle, stepper, text box, section
// labels, and a press-feedback helper. Drawn at target-local coordinates like widgets.h.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../hal/epd.h"
#include "../hal/touch.h"
#include "widgets.h"

namespace ui {

// A tappable area with its own draw function (so it can be redrawn inverted on touch-down).
struct Button {
  hal::Rect rect;
  // Draws the button at (x, y) in `g`. pressed = inverted look for the touch-down frame.
  std::function<void(Gfx& g, int x, int y, bool pressed)> draw;
  int id = 0;
  bool blackWhite = false;  // pure B/W drawing: restore with DU instead of GL16 (no blink)
};

// Touch-down feedback: the button inverts at once (DU4). On release it is redrawn normally (GL16,
// so gray borders come back) from tick(), so a screen that navigates away skips that refresh.
class PressFeedback {
 public:
  // Returns the id of the button tapped (or -1), handling the down/up frames for `buttons`.
  int handle(const hal::TouchEvent& e, const std::vector<Button>& buttons);
  void reset() { pressed_ = -1; }
  // Call from the screen's onTick: restores a released button once the panel is idle.
  void tick();
  // Queues a normal redraw of `b` (e.g. a tile whose selected state changed) with the restores.
  void queueRestore(const Button& b);
  // Drops queued restores (the screen is about to redraw that area itself).
  void clearPending() { pending_.clear(); }

 private:
  void drawButton(const Button& b, bool pressed, hal::Mode m);
  int pressed_ = -1;
  std::vector<Button> pending_;  // several keys can be released before the panel is free (M6 bug)
};

// ---- pieces ------------------------------------------------------------------------------
// 44x44 button with an icon (ui_up / ui_down / ui_trash ...), border level 7 or 0.
void drawSmallIconButton(Gfx& g, int x, int y, const char* icon, int borderLevel, bool pressed);
// Uppercase section label (18 px bold, level 3), e.g. "TASK LISTS".
void drawSectionLabel(Gfx& g, int x, int y, const char* text);
// Rounded text box: border 2/3 px, text 20-26 px bold; placeholder in level 6 when empty.
void drawTextBox(Gfx& g, const hal::Rect& r, const std::string& text, const char* placeholder, fonts::Id font,
                 int border, bool pressed);
// On/off toggle 76x44 (DESIGN_SPEC §7.2), pure black/white. (absX, absY) = target origin on screen.
void drawToggle(Gfx& g, int x, int y, bool on, int absX = 0, int absY = 0);
// "–  04:00  +" stepper: two 44 px buttons around a 76 px value.
void drawStepper(Gfx& g, int x, int y, const std::string& value);
constexpr int kStepperWidth = 44 + 12 + 76 + 12 + 44;
// Full-width dashed "+ Add ..." button.
void drawAddButton(Gfx& g, const hal::Rect& r, const char* label, bool pressed);
// "Back" 52x52 button (admin header).
void drawBackButton(Gfx& g, int x, int y, bool pressed);
// Black pill button with white text (e.g. "Save"); gray (disabled) when !enabled.
void drawDarkButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, bool enabled, bool pressed);
// White button with black border (e.g. "Cancel").
void drawLightButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, bool pressed);

// "06:30" -> "6:30" (mockup style for times in list meta lines).
std::string shortTime(int minuteOfDay);

}  // namespace ui
