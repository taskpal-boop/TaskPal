// Kid screens (DESIGN_SPEC §5): Pick a list, Checklist, All done (+ M5 animations, §6).
// kid.cpp: Pick a list + parent placeholder. checklist.cpp: Checklist + All done.
#pragma once
#include <cstdint>
#include <memory>

#include "../ui/screen.h"

namespace screens {

std::unique_ptr<ui::Screen> makePickList();
std::unique_ptr<ui::Screen> makeChecklist(uint32_t listId);
// wiggle = play the star animation (arriving from the last check); false when just re-shown.
std::unique_ptr<ui::Screen> makeCelebrate(uint32_t listId, bool wiggle = false);

}  // namespace screens
