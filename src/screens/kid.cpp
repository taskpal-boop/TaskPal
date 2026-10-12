#include "kid.h"
#include "admin.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../core/logic_day.h"
#include "../core/now_list.h"
#include "../core/stars.h"
#include "../hal/epd.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

namespace screens {

namespace {

using namespace theme;
using hal::Rect;

// ======================================================================================
// Pick a list
// ======================================================================================
class PickList : public ui::Screen {
 public:
  void render(lgfx::LovyanGFX& g) override {
    auto& s = app::state();
    const auto now = app::now();
    signature_ = makeSignature();

    // A child's name can make the greeting too wide for the space left of the gear (M6): step the
    // font down until it fits, keeping the baseline area of the 52 px design.
    const std::string hello = core::greeting(now.hour, s.settings.childName);
    const int maxW = kGear.x - 16 - kSide;
    // Largest size that fits (measured: "Good afternoon, Alex!" fits at 40 px, M7).
    const fonts::Id sizes[] = {fonts::Id::fredoka_52, fonts::Id::fredoka_46, fonts::Id::fredoka_44,
                               fonts::Id::fredoka_40, fonts::Id::fredoka_38, fonts::Id::fredoka_34,
                               fonts::Id::fredoka_30};
    fonts::Id font = sizes[6];
    for (fonts::Id f : sizes) {
      if (ui::textWidth(g, f, hello) <= maxW) {
        font = f;
        break;
      }
    }
    ui::useFont(g, fonts::Id::fredoka_52);
    const int fullH = g.fontHeight();
    ui::useFont(g, font);
    const int yOff = (fullH - g.fontHeight()) / 2;  // centred in the 52 px line
    ui::drawText(g, font, hello, kSide, 36 + yOff, lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_26b, "Pick your list", kSide, 97, lgfx::top_left, kText2);
    drawGear(g, 0, 0, 0.0f);

    const bool stars = s.settings.weeklyStars;
    const int top = 156;
    const int bottom = stars ? 818 : 932;
    const int n = int(s.lists.lists.size());
    cards_.clear();
    if (n == 0) {
      ui::drawText(g, fonts::Id::atkinson_26b, "No lists yet.", kScreenW / 2, 400, lgfx::middle_center, kInk);
      ui::drawText(g, fonts::Id::atkinson_22b, "Ask a grown-up to add one.", kScreenW / 2, 440,
                   lgfx::middle_center, kText2);
    } else {
      const int gap = 18;
      const int h = std::min(150, (bottom - top - (n - 1) * gap) / n);
      const int nowIdx = core::nowListIndex(s.lists, core::minuteOfDay(now));
      for (int i = 0; i < n; ++i) {
        const auto& l = s.lists.lists[i];
        const Rect r{kSide, int16_t(top + i * (h + gap)), kContentW, int16_t(h)};
        ui::drawListCard(g, r.x, r.y, r.w, r.h, l, core::doneCount(s.progress, l), i == nowIdx);
        cards_.push_back({r, l.id});
      }
    }
    if (stars) {
      const Rect f{kSide, 846, kContentW, 86};
      ui::dashedRoundBox(g, f.x, f.y, f.w, f.h, 22, 3, kDash);
      char text[40];
      const int n2 = app::weeklyStars();
      std::snprintf(text, sizeof text, "%d star%s this week", n2, n2 == 1 ? "" : "s");
      const int tw = ui::textWidth(g, fonts::Id::atkinson_26b, text);
      const int x0 = f.x + (f.w - (40 + 14 + tw)) / 2;
      ui::drawIcon(g, "star_fill", 40, x0, f.y + (f.h - 40) / 2, kInk, kWhite);
      ui::drawText(g, fonts::Id::atkinson_26b, text, x0 + 54, f.y + f.h / 2, lgfx::middle_left, kInk);
    }
  }

  void onTouch(const hal::TouchEvent& e) override {
    using T = hal::TouchEvent::Type;
    const bool inGear = kGearHit.contains(e.x, e.y);
    switch (e.type) {
      case T::Down:
        holding_ = inGear;
        ringStep_ = 0;
        break;
      case T::Hold:
        if (holding_) {
          holding_ = false;
          ui::screens().show(makeParentEntry());
        }
        break;
      case T::Up:
        if (holding_ || ringStep_ > 0) restoreGear();
        holding_ = false;
        break;
      case T::Tap:
        if (inGear) break;  // the gear needs a 3 s hold
        for (const auto& c : cards_) {
          if (c.rect.contains(e.x, e.y)) {
            ui::screens().show(makeChecklist(c.listId));
            return;
          }
        }
        break;
    }
  }

  void onTick(uint32_t nowMs) override {
    // Hold progress ring on the gear: one step per second (DU4, pure black).
    if (holding_ && hal::touch().pressed()) {
      const int step = int(hal::touch().heldMs(nowMs) / 1000);
      if (step > ringStep_ && step < 3) {
        ringStep_ = step;
        ui::Region r(kGearRing, kPureWhite);
        drawGear(r.canvas(), -kGearRing.x, -kGearRing.y, step / 3.0f);
        r.push(hal::Mode::Fastest);
      }
    }
    // New day, new hour greeting or a different NOW list: redraw everything.
    if (nowMs - lastCheckMs_ > 20000) {
      lastCheckMs_ = nowMs;
      const bool dayChanged = app::rolloverIfNeeded(true);
      if (dayChanged || makeSignature() != signature_) ui::screens().redraw();
    }
  }

  bool showsTime() const override { return true; }
  bool stale() const override { return makeSignature() != signature_; }

 private:
  struct Card {
    Rect rect;
    uint32_t listId;
  };
  static constexpr Rect kGear{kScreenW - kSide - 56, 36, 56, 56};
  static constexpr Rect kGearHit{kScreenW - kSide - 56 - 16, 20, 56 + 32, 56 + 32};
  static constexpr Rect kGearRing{kScreenW - kSide - 56 - 10, 26, 76, 76};

  std::string makeSignature() const {
    const auto& s = app::state();
    const auto now = app::now();
    return core::greeting(now.hour, s.settings.childName) + "|" +
           std::to_string(core::nowListIndex(s.lists, core::minuteOfDay(now))) + "|" + s.progress.day;
  }

  // (ox, oy) shift the drawing for a Region canvas; progress 0..1 draws the hold ring.
  void drawGear(lgfx::LovyanGFX& g, int ox, int oy, float progress) {
    const Rect r{int16_t(kGear.x + ox), int16_t(kGear.y + oy), kGear.w, kGear.h};
    if (progress > 0) {
      // Fast frame: pure black/white only.
      const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
      g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 16, kPureBlack);
      g.fillSmoothRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 14, kPureWhite);
      ui::drawIcon(g, "ui_sliders", 30, r.x + 13, r.y + 13, kInk, kWhite, true);
      g.fillArc(cx, cy, 37, 33, -90, -90 + 360 * progress, kPureBlack);
      return;
    }
    ui::drawIconButton(g, r, "ui_sliders", 30, 2, kBorder2, kMuted, 16);
  }

  void restoreGear() {
    ringStep_ = 0;
    ui::Region r(kGearRing);
    drawGear(r.canvas(), -kGearRing.x, -kGearRing.y, 0.0f);
    r.push(hal::Mode::Text);
  }

  std::vector<Card> cards_;
  std::string signature_;
  bool holding_ = false;
  int ringStep_ = 0;
  uint32_t lastCheckMs_ = 0;
};

}  // namespace

std::unique_ptr<ui::Screen> makePickList() { return std::make_unique<PickList>(); }

}  // namespace screens
