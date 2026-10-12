// Checklist and All-done screens with the e-ink animations (M5, DESIGN_SPEC §6).
#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../core/stars.h"
#include "../hal/epd.h"
#include "../ui/anim.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"
#include "kid.h"

namespace screens {

namespace {

using namespace theme;
using hal::Rect;

Rect intersect(const Rect& a, const Rect& b) {
  const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
  if (x1 <= x0 || y1 <= y0) return Rect{};
  return Rect{int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

// The celebration picture of a list (D27): two poses drawn in `stage` (screen coordinates; the
// corgi/poop stages must match tools/extract_mockup.py and tools/gen_icons.py). The corgi and poop
// poses are pre-halftoned pure black/white, so they can dance with DU like the star.
struct CelebrationArt {
  const char* poseA;
  const char* poseB;
  Rect stage;
  int steps;          // pose changes after the reveal, ending on pose A
  const char* line2;  // under "{List} is all done."; nullptr = "You earned {n} stars."
};

CelebrationArt artFor(core::Celebration c) {
  switch (c) {
    case core::Celebration::Corgi:
      return {"celeb_corgi_a", "celeb_corgi_b", Rect{54, 77, 432, 380}, anim::kDanceSteps,
              "Corgi is doing a happy dance!"};
    case core::Celebration::Poop:
      return {"celeb_poop_a", "celeb_poop_b", Rect{54, 102, 432, 330}, anim::kDanceSteps, "Even Poop is dancing!"};
    default:
      return {"fx_celebrate_a", "fx_celebrate_b", Rect{(kScreenW - 280) / 2, 119, 280, 280}, anim::kWiggleSteps,
              nullptr};
  }
}

core::Celebration celebrationOf(uint32_t listId) {
  const core::TaskList* l = core::findList(app::state().lists, listId);
  return l ? l->celebration : core::Celebration::Star;
}

// ======================================================================================
// Checklist
// ======================================================================================
class Checklist : public ui::Screen {
 public:
  explicit Checklist(uint32_t listId) : listId_(listId) {}

  uint32_t resumeListId() const override { return listId_; }
  bool busy() const override { return seq_.running() || !pending_.empty(); }

  void render(lgfx::LovyanGFX& g) override {
    const core::TaskList* l = list();
    if (!l) return;
    const auto now = app::now();
    ui::drawIconButton(g, kBack, "ui_back", 32, 3, kInk, kInk, 16);
    ui::drawText(g, fonts::Id::atkinson_18b, ui::weekdayName(core::dayOfWeek(now.date)), 100, 30, lgfx::top_left,
                 kText2);
    ui::drawText(g, fonts::Id::fredoka_46, l->name, 100, 52, lgfx::top_left, kInk);
    ui::drawIcon(g, l->icon.c_str(), 72, kScreenW - kSide - 72, 28, kInk, kWhite);
    drawProgress(g, 0, 0);
    layoutRows();
    for (size_t i = 0; i < rows_.size(); ++i) drawRow(g, i, 0, 0);
    ui::drawText(g, fonts::Id::atkinson_20b, "Tap a task when you finish it", kScreenW / 2, 926,
                 lgfx::middle_center, kText2);
  }

  void onTouch(const hal::TouchEvent& e) override {
    using T = hal::TouchEvent::Type;
    const bool busy = seq_.running() || !pending_.empty();
    if (e.type == T::Tap && kBackHit.contains(e.x, e.y)) {
      if (!busy) ui::screens().show(makePickList());
      return;
    }
    const int row = rowAt(e.x, e.y);
    switch (e.type) {
      case T::Down:
        if (row >= 0 && !busy) {
          downMs_ = millis();
          pressed_ = row;
          showPressed(size_t(row));
        }
        break;
      case T::Tap: {
        if (row < 0) break;
        const uint32_t now = millis();
        if (size_t(row) == lastRow_ && now - lastTapMs_ < anim::kDoubleTapGuardMs) break;  // AN-05
        lastRow_ = size_t(row);
        lastTapMs_ = now;
        if (busy) {
          if (pending_.size() < 8) pending_.push_back(size_t(row));  // AN-09: applied after the animation
        } else {
          activate(size_t(row));
        }
        break;
      }
      case T::Up:
        if (pressed_ >= 0) {  // finger slid away or held: no tap, undo the pressed border
          restoreRow(size_t(pressed_));
          pressed_ = -1;
        }
        break;
      case T::Hold:
        break;
    }
  }

  void onTick(uint32_t nowMs) override {
    seq_.tick(nowMs);
    if (!seq_.running() && !pending_.empty() && !hal::epd().busy()) {
      const size_t row = pending_.front();
      pending_.pop_front();
      activate(row);
    }
    if (!seq_.running() && nowMs - lastCheckMs_ > 20000) {
      lastCheckMs_ = nowMs;
      if (app::rolloverIfNeeded(true)) ui::screens().redraw();  // new day: all tasks open again
    }
  }

 private:
  struct Row {
    Rect rect;
    uint32_t taskId;
  };
  static constexpr Rect kBack{kSide, 36, 56, 56};
  static constexpr Rect kBackHit{0, 16, 110, 96};
  static constexpr Rect kProgress{kSide, 122, kContentW, 84};
  static constexpr int kRowsTop = 228;
  static constexpr int kRowsBottom = 900;

  const core::TaskList* list() const { return core::findList(app::state().lists, listId_); }

  int rowAt(int x, int y) const {
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (rows_[i].rect.contains(x, y)) return int(i);
    }
    return -1;
  }

  void layoutRows() {
    rows_.clear();
    const core::TaskList* l = list();
    if (!l) return;
    const int avail = kRowsBottom - kRowsTop;
    gap_ = 12;
    style_ = ui::taskRowStyle(l->tasks.size(), avail, gap_);
    if (style_.height < 100) gap_ = 8, style_ = ui::taskRowStyle(l->tasks.size(), avail, gap_);
    for (size_t i = 0; i < l->tasks.size(); ++i) {
      rows_.push_back({Rect{kSide, int16_t(kRowsTop + int(i) * (style_.height + gap_)), kContentW,
                            int16_t(style_.height)},
                       l->tasks[i].id});
    }
  }

  // ---- geometry used by the animation frames ----
  int circleX(size_t i) const { return rows_[i].rect.x + rows_[i].rect.w - 18 - style_.circle / 2; }
  int circleY(size_t i) const { return rows_[i].rect.y + rows_[i].rect.h / 2; }
  // Row plus the 120x120 ray box, clipped to half the gap above/below so neighbours are untouched.
  Rect stampRect(size_t i) const {
    const Rect& r = rows_[i].rect;
    const Rect burst{int16_t(circleX(i) - 60), int16_t(circleY(i) - 60), 120, 120};
    const Rect band{0, int16_t(r.y - gap_ / 2), kScreenW, int16_t(r.h + gap_ - (gap_ % 2))};
    return intersect(r.unite(burst), band);
  }

  // ---- drawing ----
  void drawProgress(lgfx::LovyanGFX& g, int ox, int oy) {
    const core::TaskList* l = list();
    const int total = int(l->tasks.size());
    const int done = int(core::doneCount(app::state().progress, *l));
    char left[24], count[24];
    std::snprintf(count, sizeof count, "%d of %d done", done, total);
    if (done >= total) std::snprintf(left, sizeof left, "All done!");
    else std::snprintf(left, sizeof left, "%d to go", total - done);
    const int x = kProgress.x + ox, y = kProgress.y + oy;
    ui::drawText(g, fonts::Id::atkinson_26b, count, x, y, lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_20b, left, x + kProgress.w, y + 4, lgfx::top_right, kText2);
    ui::drawStarRow(g, x, y + 40, total, done, 42, 10);
  }

  void drawRow(lgfx::LovyanGFX& g, size_t i, int ox, int oy) {
    const auto& t = list()->tasks[i];
    const Rect& r = rows_[i].rect;
    // (-ox, -oy) is where this canvas sits on screen: keeps the done-row halftone aligned (D18).
    ui::drawTaskRow(g, r.x + ox, r.y + oy, r.w, style_, t, core::isDone(app::state().progress, listId_, t.id), -ox,
                    -oy);
  }

  // Black/white row for the fast frames: white or black fill, icon + label, no tile.
  void drawRowBW(lgfx::LovyanGFX& g, size_t i, int ox, int oy, bool inverted) {
    const auto& t = list()->tasks[i];
    const Rect& r = rows_[i].rect;
    const int x = r.x + ox, y = r.y + oy, w = r.w, h = r.h;
    const int fg = inverted ? kWhite : kInk, bg = inverted ? kInk : kWhite;
    g.fillRoundRect(x, y, w, h, style_.radius, gray(kInk));
    if (!inverted) g.fillRoundRect(x + 3, y + 3, w - 6, h - 6, style_.radius - 3, gray(kWhite));
    const int tileX = x + 18, off = (style_.tile - style_.icon) / 2;
    ui::drawIcon(g, t.icon.c_str(), style_.icon, tileX + off, y + (h - style_.tile) / 2 + off, fg, bg);
    const int textX = tileX + style_.tile + 18;
    const int cx = x + w - 18 - style_.circle / 2;
    std::string l1, l2;
    const int lines = ui::wrapTwoLines(g, style_.font, t.label, (cx - style_.circle / 2 - 16) - textX, l1, l2);
    ui::useFont(g, style_.font);
    const int lh = g.fontHeight();
    if (lines == 1) {
      ui::drawText(g, style_.font, l1, textX, y + h / 2, lgfx::middle_left, fg);
    } else {
      ui::drawText(g, style_.font, l1, textX, y + h / 2 - lh / 2, lgfx::middle_left, fg);
      ui::drawText(g, style_.font, l2, textX, y + h / 2 + lh / 2, lgfx::middle_left, fg);
    }
  }

  // Frame 1 (Tap, DU4): thicken the border to 6 px the moment the finger lands (AN-01 < 150 ms).
  void showPressed(size_t i) {
    auto& epd = hal::epd();
    auto& g = epd.gfx();
    const Rect& r = rows_[i].rect;
    const int rad = style_.radius, t = 6;
    epd.setMode(hal::Mode::Fastest);
    g.fillRect(r.x + rad, r.y, r.w - 2 * rad, t, theme::kPureBlack);
    g.fillRect(r.x + rad, r.y + r.h - t, r.w - 2 * rad, t, theme::kPureBlack);
    g.fillRect(r.x, r.y + rad, t, r.h - 2 * rad, theme::kPureBlack);
    g.fillRect(r.x + r.w - t, r.y + rad, t, r.h - 2 * rad, theme::kPureBlack);
    g.fillArc(r.x + rad, r.y + rad, rad, rad - t, 180, 270, theme::kPureBlack);
    g.fillArc(r.x + r.w - rad - 1, r.y + rad, rad, rad - t, 270, 360, theme::kPureBlack);
    g.fillArc(r.x + r.w - rad - 1, r.y + r.h - rad - 1, rad, rad - t, 0, 90, theme::kPureBlack);
    g.fillArc(r.x + rad, r.y + r.h - rad - 1, rad, rad - t, 90, 180, theme::kPureBlack);
    epd.refresh(r, hal::Mode::Fastest);
    ESP_LOGI("anim", "tap frame started %lu ms after touch-down", (unsigned long)(millis() - downMs_));
  }

  void restoreRow(size_t i) {
    ui::Region r(rows_[i].rect);
    drawRow(r.canvas(), i, -rows_[i].rect.x, -rows_[i].rect.y);
    r.push(hal::Mode::Text);
  }

  // Frame 4 (Settle, DU4): the done (halftone) or open row, also wiping the ray remnants. Only this
  // row's area: a refresh reaching up to the progress block re-drove the rows in between (M5).
  void addSettleFrames(std::vector<ui::Frame>& f, size_t i) {
    const Rect area = stampRect(i);
    // Done rows use a halftone gray (D18): pure black/white, so DU shows it without the GL16 blink.
    // DU, not DU4: this frame clears black (check, rays) and DU4 left ghosts (user report, M5).
    f.push_back({area, hal::Mode::Fast, 0,
                 [this, i, area] {
                   ui::Region row(area);
                   drawRow(row.canvas(), i, -area.x, -area.y);
                   row.binarize();
                   row.pushOnly(hal::Mode::Fast);
                 },
                 "settle"});
  }

  // Progress block (count, "to go", star row) in one DU4 frame, pure black/white. DU4 only moves
  // pixels that change, so the stars already filled stay still; a GL16 here re-drove every star
  // and the text (visible blink, ~450 ms, user report M5). Text keeps hard edges until the next
  // full render.
  // clearing = a star is being removed (uncheck): use DU, which clears black cleanly.
  ui::Frame progressFrame(bool clearing) {
    const hal::Mode m = clearing ? hal::Mode::Fast : hal::Mode::Fastest;
    return ui::Frame{kProgress, m, 0,
                     [this, m] {
                       ui::Region prog(kProgress);
                       drawProgress(prog.canvas(), -kProgress.x, -kProgress.y);
                       prog.binarize();
                       prog.pushOnly(m);
                     },
                     "progress"};
  }

  std::vector<ui::Frame> checkFrames(size_t i) {
    const Rect row = rows_[i].rect;
    const Rect stamp = stampRect(i);
    const int cx = circleX(i), cy = circleY(i);
    std::vector<ui::Frame> f;
    // 2 Flash (DU): the row inverts, the intended e-ink blink.
    f.push_back({row, hal::Mode::Fast, anim::kFlashHoldMs,
                 [this, i, row] {
                   ui::Region r(row);
                   drawRowBW(r.canvas(), i, -row.x, -row.y, true);
                   const int ccx = circleX(i) - row.x, ccy = circleY(i) - row.y;
                   r.canvas().fillCircle(ccx, ccy, style_.circle / 2, gray(kWhite));
                   r.canvas().fillCircle(ccx, ccy, style_.circle / 2 - 3, gray(kInk));
                   r.binarize();
                   r.pushOnly(hal::Mode::Fast);
                 },
                 "flash"});
    // 3 Stamp (DU): an oversized check with rays.
    f.push_back({stamp, hal::Mode::Fast, anim::kStampHoldMs,
                 [this, i, stamp, cx, cy] {
                   ui::Region r(stamp);
                   auto& c = r.canvas();
                   drawRowBW(c, i, -stamp.x, -stamp.y, false);
                   ui::drawIcon(c, "fx_burst", 120, cx - 60 - stamp.x, cy - 60 - stamp.y, kInk, kWhite);
                   const int big = style_.circle / 2 + 10;
                   c.fillCircle(cx - stamp.x, cy - stamp.y, big, gray(kInk));
                   ui::drawIcon(c, "ui_check", 56, cx - 28 - stamp.x, cy - 28 - stamp.y, kWhite, kInk);
                   r.binarize();
                   r.pushOnly(hal::Mode::Fast);
                 },
                 "stamp"});
    // 3b Snap (DU): the check settles to its normal size. DU, not DU4: it removes the black rays,
    // and DU4 left ray ghosts (user report, M5).
    f.push_back({stamp, hal::Mode::Fast, anim::kSnapHoldMs,
                 [this, i, stamp, cx, cy] {
                   ui::Region r(stamp);
                   auto& c = r.canvas();
                   drawRowBW(c, i, -stamp.x, -stamp.y, false);
                   c.fillCircle(cx - stamp.x, cy - stamp.y, style_.circle / 2, gray(kInk));
                   ui::drawIcon(c, "ui_check", style_.check, cx - style_.check / 2 - stamp.x,
                                cy - style_.check / 2 - stamp.y, kWhite, kInk);
                   r.binarize();
                   r.pushOnly(hal::Mode::Fast);
                 },
                 "snap"});
    // 3c Progress (DU4): the next header star fills and the count changes.
    f.push_back(progressFrame(false));
    addSettleFrames(f, i);
    return f;
  }

  void activate(size_t i) {
    pressed_ = -1;
    const core::TaskList* l = list();
    if (!l || i >= l->tasks.size()) return;
    const bool wasDone = core::isDone(app::state().progress, listId_, l->tasks[i].id);
    app::toggleTask(listId_, l->tasks[i].id);
    if (wasDone) {  // unchecking gets no animation (DESIGN_SPEC §6.1)
      std::vector<ui::Frame> f;
      addSettleFrames(f, i);
      f.push_back(progressFrame(true));
      seq_.play(std::move(f));
      return;
    }
    seq_.play(checkFrames(i), [this] { afterCheck(); });
  }

  void afterCheck() {
    const core::TaskList* l = list();
    if (!l || !app::listComplete(*l)) return;
    pending_.clear();  // the list is finished: queued taps no longer apply
    // Finish (DESIGN_SPEC §6.2): hold, blackout with a white star, then the celebration (GC16).
    std::vector<ui::Frame> f;
    f.push_back({Rect{}, hal::Mode::Fast, anim::kFinishHoldMs, nullptr, "hold"});
    f.push_back({Rect{0, 0, kScreenW, kScreenH}, hal::Mode::Fast, anim::kBlackoutHoldMs,
                 [id = listId_] {
                   ui::Region r(Rect{0, 0, kScreenW, kScreenH}, gray(kInk));
                   const CelebrationArt art = artFor(celebrationOf(id));
                   if (art.line2) {  // corgi / poop: inverted, where it will dance
                     ui::drawIcon(r.canvas(), art.poseA, art.stage.w, art.stage.x, art.stage.y, kWhite, kInk);
                   } else {
                     ui::drawIcon(r.canvas(), "fx_celebrate_a", 280, (kScreenW - 280) / 2, 340, kWhite, kInk);
                   }
                   r.binarize();
                   r.pushOnly(hal::Mode::Fast);
                 },
                 "blackout"});
    const uint32_t id = listId_;
    seq_.play(std::move(f), [id] { ui::screens().show(makeCelebrate(id, true)); });
  }

  uint32_t listId_;
  ui::TaskRowStyle style_{};
  int gap_ = 12;
  std::vector<Row> rows_;
  ui::Sequencer seq_;
  std::deque<size_t> pending_;
  int pressed_ = -1;
  size_t lastRow_ = SIZE_MAX;
  uint32_t lastTapMs_ = 0;
  uint32_t downMs_ = 0;
  uint32_t lastCheckMs_ = 0;
};

// ======================================================================================
// All done (celebration)
// ======================================================================================
class Celebrate : public ui::Screen {
 public:
  Celebrate(uint32_t listId, bool wiggle)
      : listId_(listId), wiggle_(wiggle), art_(artFor(celebrationOf(listId))) {}

  bool busy() const override { return seq_.running() || (wiggle_ && !started_); }

  void render(lgfx::LovyanGFX& g) override {
    const core::TaskList* l = core::findList(app::state().lists, listId_);
    const int n = l ? int(l->tasks.size()) : 0;
    if (!art_.line2) {
      // Decorations (DESIGN_SPEC §5.3); the corgi and poop bring their own notes and stars.
      ui::drawIcon(g, "star_fill", 28, 60, 110, kInk, kWhite);
      ui::drawIcon(g, "star_fill", 22, kScreenW - 70 - 22, 150, kInk, kWhite);
      g.fillTriangle(96 + 9, 700 - 4, 96 - 4, 700 + 9, 96 + 22, 700 + 9, gray(kInk));  // diamond, top half
      g.fillTriangle(96 - 4, 700 + 9, 96 + 22, 700 + 9, 96 + 9, 700 + 22, gray(kInk));  // bottom half
      g.fillSmoothCircle(kScreenW - 90 - 11, 760 + 11, 11, gray(kInk));
      g.fillSmoothCircle(kScreenW - 90 - 11, 760 + 11, 7, gray(kWhite));
      ui::drawIcon(g, "star", 34, kScreenW - 60 - 34, 560, kInk, kWhite);
      g.fillSmoothCircle(50 + 8, 440 + 8, 8, gray(kDash));
    }

    const Rect& st = art_.stage;
    ui::drawIcon(g, art_.poseA, st.w, st.x, st.y, kInk, kWhite);
    int y = st.y + st.h + 24;
    ui::drawText(g, fonts::Id::fredoka_68, "You did it!", kScreenW / 2, y, lgfx::top_center, kInk);
    y += 71 + 24;
    char line1[48], line2[48];
    std::snprintf(line1, sizeof line1, "%s is all done.", l ? l->name.c_str() : "The list");
    if (art_.line2) {
      std::snprintf(line2, sizeof line2, "%s", art_.line2);
    } else {
      std::snprintf(line2, sizeof line2, "You earned %d star%s.", n, n == 1 ? "" : "s");
    }
    ui::drawText(g, fonts::Id::atkinson_30b, line1, kScreenW / 2, y, lgfx::top_center, kInk);
    ui::drawText(g, fonts::Id::atkinson_30b, line2, kScreenW / 2, y + 36, lgfx::top_center, kInk);
    y += 72 + 24;
    const int starsW = n * 40 + (n - 1) * 10;
    ui::drawStarRow(g, (kScreenW - starsW) / 2, y, n, n, 40, 10);
    y += 40 + 24 + 16;
    back_ = Rect{40, int16_t(y), 460, 84};
    again_ = Rect{40, int16_t(y + 84 + 14), 460, 56};
    ui::drawPrimaryButton(g, back_, "Back to my lists", fonts::Id::atkinson_30b, 22);
    ui::drawSecondaryButton(g, again_, "Start this list again", fonts::Id::atkinson_22b, 16);
  }

  void onTouch(const hal::TouchEvent& e) override {
    if (e.type != hal::TouchEvent::Type::Tap) return;
    if (back_.contains(e.x, e.y)) {
      seq_.cancel();
      ui::screens().show(makePickList());
    } else if (again_.contains(e.x, e.y)) {
      seq_.cancel();
      app::restartList(listId_);
      ui::screens().show(makeChecklist(listId_));
    }
  }

  void onTick(uint32_t nowMs) override {
    if (wiggle_ && !started_ && !hal::epd().busy()) {  // start once the GC16 reveal is on screen
      started_ = true;
      seq_.play(wiggleFrames());
    }
    seq_.tick(nowMs);
  }

 private:
  // Wiggle / dance (DU, pure black/white): the star's rays turn 22.5 degrees and it hops; the
  // corgi sways and the poop bounces (D27). DU4 left visible traces of the previous pose (user
  // report, M5); DU does not.
  std::vector<ui::Frame> wiggleFrames() {
    std::vector<ui::Frame> f;
    f.push_back({Rect{}, hal::Mode::Fastest, anim::kWiggleMs, nullptr, "wait"});
    const Rect st = art_.stage;
    for (int k = 0; k < art_.steps; ++k) {
      const char* pose = (k % 2 == 0) ? art_.poseB : art_.poseA;
      f.push_back({st, hal::Mode::Fast, anim::kWiggleMs,
                   [pose, st] {
                     ui::Region r(st);
                     ui::drawIcon(r.canvas(), pose, st.w, 0, 0, kInk, kWhite);
                     r.binarize();
                     r.pushOnly(hal::Mode::Fast);
                   },
                   "wiggle"});
    }
    // No cleanup refresh: DU leaves no visible ghosting, and a final GC16 flashed the star box
    // black (user report, M5). The wiggle ends on pose A (even number of steps).
    return f;
  }

  uint32_t listId_;
  bool wiggle_;
  CelebrationArt art_;
  bool started_ = false;
  Rect back_, again_;
  ui::Sequencer seq_;
};

}  // namespace

std::unique_ptr<ui::Screen> makeChecklist(uint32_t listId) { return std::make_unique<Checklist>(listId); }
std::unique_ptr<ui::Screen> makeCelebrate(uint32_t listId, bool wiggle) {
  return std::make_unique<Celebrate>(listId, wiggle);
}

}  // namespace screens
