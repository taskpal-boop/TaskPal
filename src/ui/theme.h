// Visual constants confirmed on the device (DESIGN_SPEC §1-2, D11, D12, EINK_TUNING_LOG).
#pragma once
#include <cstdint>

namespace theme {

// Panel level L (0 = black .. 15 = white) as an RGB888 gray that M5GFX maps exactly to L,
// with no Bayer dither, in Quality/Text frames: v = 16L + 8.
constexpr uint8_t grayValue(int level) { return uint8_t(16 * level + 8); }
constexpr uint32_t gray(int level) {
  return uint32_t(grayValue(level)) << 16 | uint32_t(grayValue(level)) << 8 | grayValue(level);
}

// Fast/Fastest (DU/DU4) frames may use only these two: anything else is dithered into dots.
constexpr uint32_t kPureBlack = 0x000000u;
constexpr uint32_t kPureWhite = 0xFFFFFFu;

// Palette levels (D11).
constexpr int kWhite = 15;
constexpr int kBg = 15;
constexpr int kTile = 13;
constexpr int kDone = 11;
constexpr int kDivider = 9;
constexpr int kBorder2 = 7;
constexpr int kDash = 6;
constexpr int kMuted = 5;
constexpr int kText2 = 3;
constexpr int kInk = 0;

// Layout.
constexpr int kScreenW = 540;
constexpr int kScreenH = 960;
constexpr int kSide = 28;           // side padding
constexpr int kContentW = 484;      // 540 - 2 * 28
constexpr int kMinTouch = 56;       // smallest hit target

// Animation timings (DESIGN_SPEC §6; tune on the device and log in EINK_TUNING_LOG §7).
// Holds count from the moment a frame's refresh has finished.
namespace anim {
constexpr uint32_t kFlashHoldMs = 120;     // inverted row stays up this long
constexpr uint32_t kStampHoldMs = 120;     // big check + rays
constexpr uint32_t kSnapHoldMs = 0;
constexpr uint32_t kFinishHoldMs = 600;    // after the last check, before the blackout
constexpr uint32_t kBlackoutHoldMs = 500;
constexpr uint32_t kWiggleMs = 290;        // hold per pose; + ~310 ms draw + DU refresh = ~600 ms period
constexpr int kWiggleSteps = 4;            // then stop (battery)
constexpr int kDanceSteps = 6;             // corgi / poop dance (D27): 3 moves, ending on pose A (to tune)
constexpr uint32_t kDoubleTapGuardMs = 700;
}  // namespace anim

}  // namespace theme
