#include "controls.h"

#include <algorithm>

#include "fonts.h"
#include "icons.h"
#include "region.h"
#include "theme.h"

namespace ui {

using namespace theme;

// ---- press feedback ----------------------------------------------------------------------
void PressFeedback::drawButton(const Button& b, bool pressed, hal::Mode m) {
  Region r(b.rect);
  b.draw(r.canvas(), 0, 0, pressed);
  if (m == hal::Mode::Fastest || m == hal::Mode::Fast) r.binarize();
  r.push(m);
}

int PressFeedback::handle(const hal::TouchEvent& e, const std::vector<Button>& buttons) {
  using T = hal::TouchEvent::Type;
  auto find = [&](int x, int y) {
    for (size_t i = 0; i < buttons.size(); ++i) {
      if (buttons[i].rect.contains(x, y)) return int(i);
    }
    return -1;
  };
  switch (e.type) {
    case T::Down: {
      pressed_ = find(e.x, e.y);
      if (pressed_ >= 0) drawButton(buttons[pressed_], true, hal::Mode::Fastest);
      return -1;
    }
    case T::Tap: {
      const int i = find(e.x, e.y);
      if (pressed_ >= 0 && pressed_ < int(buttons.size())) queueRestore(buttons[pressed_]);
      pressed_ = -1;
      return i >= 0 ? buttons[i].id : -1;
    }
    case T::Up:
      if (pressed_ >= 0 && pressed_ < int(buttons.size())) queueRestore(buttons[pressed_]);
      pressed_ = -1;
      return -1;
    default:
      return -1;
  }
}

void PressFeedback::queueRestore(const Button& b) {
  for (const auto& p : pending_) {
    if (p.id == b.id) return;
  }
  pending_.push_back(b);
}

void PressFeedback::tick() {
  if (pending_.empty() || hal::epd().busy()) return;
  const Button b = pending_.front();
  pending_.erase(pending_.begin());
  drawButton(b, false, b.blackWhite ? hal::Mode::Fast : hal::Mode::Text);
}

// ---- pieces ------------------------------------------------------------------------------
void drawSmallIconButton(Gfx& g, int x, int y, const char* icon, int borderLevel, bool pressed) {
  const int fill = pressed ? kInk : kWhite, ink = pressed ? kWhite : kInk;
  roundBox(g, x, y, 44, 44, 12, 2, pressed ? kInk : borderLevel, fill);
  drawIcon(g, icon, 28, x + 8, y + 8, ink, fill);
}

void drawSectionLabel(Gfx& g, int x, int y, const char* text) {
  drawText(g, fonts::Id::atkinson_18b, text, x, y, lgfx::top_left, kText2);
}

void drawTextBox(Gfx& g, const hal::Rect& r, const std::string& text, const char* placeholder, fonts::Id font,
                 int border, bool pressed) {
  roundBox(g, r.x, r.y, r.w, r.h, 12, border, kInk, pressed ? kTile : kWhite);
  if (text.empty()) {
    drawText(g, font, placeholder ? placeholder : "", r.x + 14, r.y + r.h / 2, lgfx::middle_left, kDash);
  } else {
    drawText(g, font, text, r.x + 14, r.y + r.h / 2, lgfx::middle_left, kInk);
  }
}

void drawToggle(Gfx& g, int x, int y, bool on, int absX, int absY) {
  // Pure black/white (off track = halftone, D18) so it can change with DU and no blink (M6).
  roundBox(g, x, y, 76, 44, 22, 3, kInk, on ? kInk : kWhite);
  if (!on) halftoneRoundRect(g, x + 3, y + 3, 70, 38, 19, kDone, absX, absY);
  const int kx = on ? x + 76 - 6 - 32 : x + 6;
  g.fillSmoothCircle(kx + 16, y + 22, 16, gray(kInk));
  g.fillSmoothCircle(kx + 16, y + 22, 14, gray(kWhite));
}

void drawStepper(Gfx& g, int x, int y, const std::string& value) {
  roundBox(g, x, y, 44, 44, 12, 2, kBorder2, kWhite);
  drawText(g, fonts::Id::atkinson_26b, "\xE2\x80\x93", x + 22, y + 21, lgfx::middle_center, kInk);  // en dash
  drawText(g, fonts::Id::atkinson_26b, value, x + 44 + 12 + 38, y + 22, lgfx::middle_center, kInk);
  const int px = x + 44 + 12 + 76 + 12;
  roundBox(g, px, y, 44, 44, 12, 2, kBorder2, kWhite);
  drawText(g, fonts::Id::atkinson_26b, "+", px + 22, y + 21, lgfx::middle_center, kInk);
}

void drawAddButton(Gfx& g, const hal::Rect& r, const char* label, bool pressed) {
  if (pressed) {
    g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 18, gray(kInk));
  } else {
    dashedRoundBox(g, r.x, r.y, r.w, r.h, 18, 3, kInk);
  }
  const int ink = pressed ? kWhite : kInk, bg = pressed ? kInk : kWhite;
  const int tw = textWidth(g, fonts::Id::atkinson_22b, label);
  const int x0 = r.x + (r.w - (28 + 10 + tw)) / 2;
  drawIcon(g, "ui_plus", 28, x0, r.y + (r.h - 28) / 2, ink, bg);
  drawText(g, fonts::Id::atkinson_22b, label, x0 + 38, r.y + r.h / 2, lgfx::middle_left, ink);
}

void drawBackButton(Gfx& g, int x, int y, bool pressed) {
  const int fill = pressed ? kInk : kWhite;
  roundBox(g, x, y, 52, 52, 14, 3, kInk, fill);
  drawIcon(g, "ui_back", 28, x + 12, y + 12, pressed ? kWhite : kInk, fill);
}

void drawDarkButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, bool enabled, bool pressed) {
  if (!enabled) {
    roundBox(g, r.x, r.y, r.w, r.h, 14, 3, kDivider, kTile);
    drawText(g, font, label, r.x + r.w / 2, r.y + r.h / 2, lgfx::middle_center, kMuted);
    return;
  }
  const int fill = pressed ? kWhite : kInk;
  roundBox(g, r.x, r.y, r.w, r.h, 14, pressed ? 3 : 0, kInk, fill);
  drawText(g, font, label, r.x + r.w / 2, r.y + r.h / 2, lgfx::middle_center, pressed ? kInk : kWhite);
}

void drawLightButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, bool pressed) {
  const int fill = pressed ? kInk : kWhite;
  roundBox(g, r.x, r.y, r.w, r.h, 12, 2, kInk, fill);
  drawText(g, font, label, r.x + r.w / 2, r.y + r.h / 2, lgfx::middle_center, pressed ? kWhite : kInk);
}

std::string shortTime(int minuteOfDay) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%d:%02d", minuteOfDay / 60, minuteOfDay % 60);
  return buf;
}

}  // namespace ui
