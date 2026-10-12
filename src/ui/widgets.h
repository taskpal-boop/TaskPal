// Reusable drawing pieces for the TaskPal screens (DESIGN_SPEC §5). All draw into any
// LovyanGFX target (full-screen canvas or a ui::Region) at target-local coordinates, using
// palette levels (theme.h). They are meant for Quality/Text frames (gray levels, blended icons).
#pragma once
#include <M5Unified.h>

#include <cstddef>
#include <string>

#include "../core/model.h"
#include "../hal/epd.h"
#include "font_ids.h"

namespace ui {

using Gfx = lgfx::LovyanGFX;

void drawText(Gfx& g, fonts::Id font, const std::string& s, int x, int y, lgfx::textdatum_t datum, int level);
int textWidth(Gfx& g, fonts::Id font, const std::string& s);

// Splits `s` at a space so each part fits maxW; returns 1 or 2 lines.
int wrapTwoLines(Gfx& g, fonts::Id font, const std::string& s, int maxW, std::string& line1, std::string& line2);

// Rounded box with a solid border (border 0 = no border).
void roundBox(Gfx& g, int x, int y, int w, int h, int radius, int border, int borderLevel, int fillLevel);
// Rounded outline with a dashed border (straight edges dashed, corners solid).
void dashedRoundBox(Gfx& g, int x, int y, int w, int h, int radius, int thickness, int level);
// Rounded area filled with a pure black/white 4x4 ordered-dither pattern that reads as gray level.
// Looks the same in every refresh mode, so it can be shown with DU4 (no GL16 blink, D18).
void halftoneRoundRect(Gfx& g, int x, int y, int w, int h, int radius, int level, int absX, int absY);

// ---- task row -------------------------------------------------------------------------
struct TaskRowStyle {
  int height;
  int radius;
  int tile;
  int icon;
  int circle;
  int check;
  fonts::Id font;
};
// Normal rows are 100 px (6 fit); lists of 7-8 tasks use compact rows (Q6 resolution).
TaskRowStyle taskRowStyle(size_t taskCount, int availableHeight, int gap);
// (absX, absY) = where the target's (0, 0) is on screen, so the halftone of done rows lines up
// whether the row is drawn in the full-screen canvas or in a Region (D18).
void drawTaskRow(Gfx& g, int x, int y, int w, const TaskRowStyle& st, const core::Task& t, bool done,
                 int absX = 0, int absY = 0);

// ---- small pieces -------------------------------------------------------------------------
void drawStarRow(Gfx& g, int x, int y, int count, int filled, int size, int gap);
void drawSegments(Gfx& g, int x, int y, int w, int total, int done);
void drawNowPill(Gfx& g, int x, int yCenter);  // returns nothing; pill is about 58x26
int nowPillWidth(Gfx& g);

// ---- list card (Pick a list) --------------------------------------------------------------
void drawListCard(Gfx& g, int x, int y, int w, int h, const core::TaskList& list, size_t done, bool isNow);

// ---- buttons ------------------------------------------------------------------------------
void drawIconButton(Gfx& g, const hal::Rect& r, const char* icon, int iconSize, int border, int borderLevel,
                    int iconLevel, int radius);
void drawPrimaryButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, int radius);
void drawSecondaryButton(Gfx& g, const hal::Rect& r, const char* label, fonts::Id font, int radius);

const char* weekdayName(int dow);  // 0 = Sunday -> "SUNDAY"

}  // namespace ui
