// Draws the generated 4bpp coverage icons (icons_data.*) onto a canvas or the display.
// Use a canvas: drawing per pixel straight to the panel is slow (SP-04 finding).
#pragma once
#include "icons_data.h"
#include "theme.h"

namespace ui {

// Quality/Text frames: ink is blended over bg with the coverage (anti-aliased edges).
// binary = true for Fast/Fastest frames: pure black/white, coverage >= 8 counts as ink (SP-09).
template <class Gfx>
bool drawIcon(Gfx& g, const char* name, int size, int x, int y, int inkLevel, int bgLevel,
              bool binary = false) {
  const icons::Bitmap* b = icons::find(icons::fromName(name), uint16_t(size));
  if (!b) return false;
  const int ink = binary ? (inkLevel < 8 ? 0 : 255) : theme::grayValue(inkLevel);
  const int bg = binary ? (bgLevel < 8 ? 0 : 255) : theme::grayValue(bgLevel);
  const int stride = (b->w + 1) / 2;
  for (int r = 0; r < b->h; ++r) {
    for (int c = 0; c < b->w; ++c) {
      const uint8_t byte = b->data[r * stride + c / 2];
      const int a = (c & 1) ? (byte & 0x0F) : (byte >> 4);
      if (a == 0 || (binary && a < 8)) continue;
      const int v = binary ? ink : bg + (ink - bg) * a / 15;
      g.drawPixel(x + c, y + r, uint32_t(v) << 16 | uint32_t(v) << 8 | uint32_t(v));
    }
  }
  return true;
}

}  // namespace ui
