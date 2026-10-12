#include "fonts.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace ui {

namespace {
const uint8_t* gData[size_t(fonts::Id::count)] = {};
}

bool loadFonts() {
  bool allPsram = true;
  for (size_t i = 0; i < size_t(fonts::Id::count); ++i) {
    if (gData[i]) continue;
    const auto& blob = fonts::kBlobs[i];
    auto* copy = static_cast<uint8_t*>(heap_caps_malloc(blob.size(), MALLOC_CAP_SPIRAM));
    if (copy) {
      std::memcpy(copy, blob.start, blob.size());
      gData[i] = copy;
    } else {
      gData[i] = blob.start;  // still works, just slower glyph reads from flash
      allPsram = false;
    }
  }
  return allPsram;
}

const uint8_t* fontData(fonts::Id id) {
  const auto i = size_t(id);
  return gData[i] ? gData[i] : fonts::kBlobs[i].start;
}

}  // namespace ui
