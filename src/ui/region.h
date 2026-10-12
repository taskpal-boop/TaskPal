// Off-screen composition of one screen area (M3 finding: drawing text straight to the panel costs
// ~125 ms per line; composing in PSRAM and pushing once is the rule for every screen).
#pragma once
#include <M5Unified.h>

#include "../hal/epd.h"
#include "theme.h"

namespace ui {

class Region {
 public:
  // Allocates a gray8 PSRAM canvas the size of `r`, cleared to `bg`.
  explicit Region(const hal::Rect& r, uint32_t bg = theme::gray(theme::kWhite));
  ~Region();

  M5Canvas& canvas() { return canvas_; }
  const hal::Rect& rect() const { return rect_; }
  bool ok() const { return ok_; }

  // Fast (DU/DU4) frames: force every pixel to pure black or white, so antialiased text and icons
  // come out crisp instead of Bayer-dithered (SP-09). Call after drawing, before pushOnly/push.
  void binarize(uint8_t threshold = 128);

  // Writes the canvas to the panel memory in mode `m` (M5GFX converts at write time) ...
  void pushOnly(hal::Mode m);
  // ... and refreshes that area with the same mode.
  void push(hal::Mode m);

 private:
  hal::Rect rect_;
  M5Canvas canvas_;
  bool ok_ = false;
};

}  // namespace ui
