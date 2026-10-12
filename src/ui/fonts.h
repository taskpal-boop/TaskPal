// VLW fonts embedded in flash (D15), copied to PSRAM once at boot for fast glyph reads.
#pragma once
#include <cstdint>

#include "font_ids.h"

namespace ui {

bool loadFonts();                          // call once after M5.begin()
const uint8_t* fontData(fonts::Id id);     // PSRAM copy (flash if PSRAM failed)

// Selects a font on a display or canvas (each keeps its own current font).
template <class Gfx>
void useFont(Gfx& g, fonts::Id id) {
  g.loadFont(fontData(id));
}

}  // namespace ui
