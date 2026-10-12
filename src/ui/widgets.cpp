#include "widgets.h"

#include <algorithm>
#include <cmath>

#include "fonts.h"
#include "icons.h"
#include "theme.h"

namespace ui {

using namespace theme;

void drawText(Gfx& g, fonts::Id font, const std::string& s, int x, int y, lgfx::textdatum_t datum, int level) {
  useFont(g, font);
  g.setTextDatum(datum);
  g.setTextColor(gray(level));
  g.drawString(s.c_str(), x, y);
}

int textWidth(Gfx& g, fonts::Id font, const std::string& s) {
  useFont(g, font);
  return g.textWidth(s.c_str());
}

int wrapTwoLines(Gfx& g, fonts::Id font, const std::string& s, int maxW, std::string& line1, std::string& line2) {
  useFont(g, font);
  line2.clear();
  if (g.textWidth(s.c_str()) <= maxW) {
    line1 = s;
    return 1;
  }
  size_t cut = s.rfind(' ');
  while (cut != std::string::npos && cut > 0 && g.textWidth(s.substr(0, cut).c_str()) > maxW) {
    cut = s.rfind(' ', cut - 1);
  }
  if (cut == std::string::npos || cut == 0) {  // one long word: keep it on one line
    line1 = s;
    return 1;
  }
  line1 = s.substr(0, cut);
  line2 = s.substr(cut + 1);
  return 2;
}

void roundBox(Gfx& g, int x, int y, int w, int h, int radius, int border, int borderLevel, int fillLevel) {
  if (border > 0) {
    g.fillSmoothRoundRect(x, y, w, h, radius, gray(borderLevel));
    g.fillSmoothRoundRect(x + border, y + border, w - 2 * border, h - 2 * border, std::max(0, radius - border),
                          gray(fillLevel));
  } else {
    g.fillSmoothRoundRect(x, y, w, h, radius, gray(fillLevel));
  }
}

void dashedRoundBox(Gfx& g, int x, int y, int w, int h, int radius, int thickness, int level) {
  const uint32_t c = gray(level);
  constexpr int kDash = 12, kGap = 8;
  for (int px = x + radius; px < x + w - radius; px += kDash + kGap) {
    const int len = std::min(kDash, x + w - radius - px);
    g.fillRect(px, y, len, thickness, c);
    g.fillRect(px, y + h - thickness, len, thickness, c);
  }
  for (int py = y + radius; py < y + h - radius; py += kDash + kGap) {
    const int len = std::min(kDash, y + h - radius - py);
    g.fillRect(x, py, thickness, len, c);
    g.fillRect(x + w - thickness, py, thickness, len, c);
  }
  const int r0 = radius, r1 = radius - thickness + 1;
  g.fillArc(x + radius, y + radius, r0, r1, 180, 270, c);
  g.fillArc(x + w - radius - 1, y + radius, r0, r1, 270, 360, c);
  g.fillArc(x + w - radius - 1, y + h - radius - 1, r0, r1, 0, 90, c);
  g.fillArc(x + radius, y + h - radius - 1, r0, r1, 90, 180, c);
}

void halftoneRoundRect(Gfx& g, int x, int y, int w, int h, int radius, int level, int absX, int absY) {
  static constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  const int black = (16 * (15 - level) + 7) / 15;  // dots per 4x4 cell: L11 -> 4 (73 % white)
  g.fillRoundRect(x, y, w, h, radius, kPureWhite);
  for (int yy = 0; yy < h; ++yy) {
    int inset = 0;
    const int fromEdge = std::min(yy, h - 1 - yy);
    if (fromEdge < radius) {
      const float dy = radius - fromEdge - 0.5f;
      inset = int(radius - std::sqrt(float(radius * radius) - dy * dy) + 0.5f);
    }
    const int ay = y + yy + absY;
    for (int xx = inset; xx < w - inset; ++xx) {
      const int ax = x + xx + absX;
      if (kBayer[(ay & 3) * 4 + (ax & 3)] < black) g.drawPixel(x + xx, y + yy, kPureBlack);
    }
  }
}

// ---- task row -------------------------------------------------------------------------
TaskRowStyle taskRowStyle(size_t taskCount, int availableHeight, int gap) {
  const int n = int(std::max<size_t>(taskCount, 1));
  const int fit = (availableHeight - (n - 1) * gap) / n;
  if (fit >= 100) return {100, 22, 68, 48, 60, 40, fonts::Id::atkinson_42b};
  // Compact (7-8 tasks): 34 px label, still allowed to wrap onto two lines.
  const int h = std::max(fit, 70);
  return {h, 18, 56, 44, 52, 34, fonts::Id::atkinson_34b};
}

void drawTaskRow(Gfx& g, int x, int y, int w, const TaskRowStyle& st, const core::Task& t, bool done, int absX,
                 int absY) {
  const int h = st.height;
  const int bg = done ? kDone : kWhite;
  const int ink = done ? kText2 : kInk;
  roundBox(g, x, y, w, h, st.radius, 3, kInk, kWhite);
  if (done) halftoneRoundRect(g, x + 3, y + 3, w - 6, h - 6, st.radius - 3, kDone, absX, absY);

  const int tileX = x + 18, tileY = y + (h - st.tile) / 2;
  if (!done) g.fillSmoothRoundRect(tileX, tileY, st.tile, st.tile, 16, gray(kTile));
  const int iconOff = (st.tile - st.icon) / 2;
  drawIcon(g, t.icon.c_str(), st.icon, tileX + iconOff, tileY + iconOff, ink, done ? bg : kTile);

  const int cx = x + w - 18 - st.circle / 2, cy = y + h / 2;
  if (done) {
    g.fillSmoothCircle(cx, cy, st.circle / 2, gray(kInk));
    drawIcon(g, "ui_check", st.check, cx - st.check / 2, cy - st.check / 2, kWhite, kInk);
  } else {
    g.fillSmoothCircle(cx, cy, st.circle / 2, gray(kInk));
    g.fillSmoothCircle(cx, cy, st.circle / 2 - 3, gray(kWhite));
  }

  const int textX = tileX + st.tile + 18;
  const int maxW = (cx - st.circle / 2 - 16) - textX;
  std::string l1, l2;
  const int lines = wrapTwoLines(g, st.font, t.label, maxW, l1, l2);
  useFont(g, st.font);
  const int lh = g.fontHeight();
  if (lines == 1) {
    drawText(g, st.font, l1, textX, cy, lgfx::middle_left, ink);
  } else {
    drawText(g, st.font, l1, textX, cy - lh / 2, lgfx::middle_left, ink);
    drawText(g, st.font, l2, textX, cy + lh / 2, lgfx::middle_left, ink);
  }
}

// ---- small pieces -------------------------------------------------------------------------
void drawStarRow(Gfx& g, int x, int y, int count, int filled, int size, int gap) {
  for (int i = 0; i < count; ++i) {
    drawIcon(g, i < filled ? "star_fill" : "star", size, x + i * (size + gap), y, kInk, kWhite);
  }
}

void drawSegments(Gfx& g, int x, int y, int w, int total, int done) {
  if (total <= 0) return;
  constexpr int kGap = 6, kH = 12;
  const int segW = (w - (total - 1) * kGap) / total;
  for (int i = 0; i < total; ++i) {
    const int sx = x + i * (segW + kGap);
    if (i < done) {
      g.fillSmoothRoundRect(sx, y, segW, kH, 6, gray(kInk));
    } else {
      roundBox(g, sx, y, segW, kH, 6, 2, kInk, kWhite);
    }
  }
}

int nowPillWidth(Gfx& g) { return textWidth(g, fonts::Id::atkinson_18b, "NOW") + 20; }

void drawNowPill(Gfx& g, int x, int yCenter) {
  const int w = nowPillWidth(g), h = 26;
  g.fillSmoothRoundRect(x, yCenter - h / 2, w, h, h / 2, gray(kInk));
  drawText(g, fonts::Id::atkinson_18b, "NOW", x + w / 2, yCenter, lgfx::middle_center, kWhite);
}

// ---- list card --------------------------------------------------------------------------
void drawListCard(Gfx& g, int x, int y, int w, int h, const core::TaskList& list, size_t done, bool isNow) {
  const bool full = h >= 140;
  const int border = isNow ? 5 : 3;
  const int pad = full ? 22 : 14;
  roundBox(g, x, y, w, h, 26, border, kInk, kWhite);

  const int tile = full ? 100 : std::min(h - 2 * pad, 80);
  const int tileX = x + pad, tileY = y + (h - tile) / 2;
  const int iconSize = full ? (isNow ? 68 : 64) : 40;
  g.fillSmoothRoundRect(tileX, tileY, tile, tile, 22, gray(isNow ? kInk : kTile));
  drawIcon(g, list.icon.c_str(), iconSize, tileX + (tile - iconSize) / 2, tileY + (tile - iconSize) / 2,
           isNow ? kWhite : kInk, isNow ? kInk : kTile);

  const int tx = tileX + tile + 20;
  const size_t total = list.tasks.size();
  char meta[32];
  if (total == 0) {
    std::snprintf(meta, sizeof meta, "No tasks yet");
  } else if (done == total) {
    std::snprintf(meta, sizeof meta, "All done!");
  } else if (done > 0 || isNow) {
    std::snprintf(meta, sizeof meta, "%u of %u done", unsigned(done), unsigned(total));
  } else {
    std::snprintf(meta, sizeof meta, "%u task%s", unsigned(total), total == 1 ? "" : "s");
  }
  const fonts::Id nameFont = full ? fonts::Id::atkinson_38b : fonts::Id::atkinson_30b;
  useFont(g, nameFont);
  const int nameH = g.fontHeight();
  const bool segments = isNow && full && total > 0;
  const int block = nameH + 10 + 24 + (segments ? 10 + 12 : 0);
  int ty = y + (h - block) / 2;

  drawText(g, nameFont, list.name, tx, ty, lgfx::top_left, kInk);
  if (isNow) drawNowPill(g, tx + textWidth(g, nameFont, list.name) + 12, ty + nameH / 2);
  ty += nameH + 10;
  drawText(g, fonts::Id::atkinson_22b, meta, tx, ty, lgfx::top_left, (isNow || done > 0) ? kInk : kText2);
  ty += 24 + 10;
  if (segments) drawSegments(g, tx, ty, x + w - pad - tx, int(total), int(done));
  if (!isNow) drawIcon(g, "ui_chevron_right", 32, x + w - pad - 32, y + (h - 32) / 2, kInk, kWhite);
}

// ---- buttons ------------------------------------------------------------------------------
void drawIconButton(Gfx& g, const hal::Rect& r, const char* icon, int iconSize, int border, int borderLevel,
                    int iconLevel, int radius) {
  roundBox(g, r.x, r.y, r.w, r.h, radius, border, borderLevel, kWhite);
  drawIcon(g, icon, iconSize, r.x + (r.w - iconSize) / 2, r.y + (r.h - iconSize) / 2, iconLevel, kWhite);
}

void drawPrimaryButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, int radius) {
  g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, radius, gray(kInk));
  drawText(g, font, label, r.x + r.w / 2, r.y + r.h / 2, lgfx::middle_center, kWhite);
}

void drawSecondaryButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, int radius) {
  roundBox(g, r.x, r.y, r.w, r.h, radius, 3, kInk, kWhite);
  drawText(g, font, label, r.x + r.w / 2, r.y + r.h / 2, lgfx::middle_center, kInk);
}

const char* weekdayName(int dow) {
  static const char* kNames[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  return kNames[((dow % 7) + 7) % 7];
}

}  // namespace ui
