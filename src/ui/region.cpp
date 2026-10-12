#include "region.h"

namespace ui {

Region::Region(const hal::Rect& r, uint32_t bg) : rect_(r), canvas_(&hal::epd().gfx()) {
  canvas_.setColorDepth(lgfx::grayscale_8bit);
  canvas_.setPsram(true);
  ok_ = canvas_.createSprite(r.w, r.h) != nullptr;
  if (ok_) canvas_.fillSprite(bg);
}

Region::~Region() { canvas_.deleteSprite(); }

void Region::binarize(uint8_t threshold) {
  if (!ok_) return;
  auto* p = static_cast<uint8_t*>(canvas_.getBuffer());
  const size_t n = size_t(rect_.w) * size_t(rect_.h);
  for (size_t i = 0; i < n; ++i) p[i] = p[i] < threshold ? 0 : 255;
}

void Region::pushOnly(hal::Mode m) {
  if (!ok_) return;
  hal::epd().setMode(m);
  canvas_.pushSprite(rect_.x, rect_.y);
}

void Region::push(hal::Mode m) {
  pushOnly(m);
  hal::epd().refresh(rect_, m);
}

}  // namespace ui
