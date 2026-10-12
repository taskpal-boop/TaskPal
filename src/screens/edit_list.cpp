// Edit list (DESIGN_SPEC §7.3) with its helper screens: Change picture (§7.4), New task (§7.4 +
// keyboard §7.5) and List time (Q2). All edits go to a draft that is saved only on "Save".
// Helper screens are full screens rather than sheets over a dimmed list (D21).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../core/catalog.h"
#include "../core/now_list.h"
#include "../core/suggest.h"
#include "../core/text_rules.h"
#include "../ui/controls.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/keyboard.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "admin.h"
#include "kid.h"

namespace screens {

using namespace theme;
using hal::Rect;

EditSession& editSession() {
  static EditSession s;
  return s;
}

void beginEditList(uint32_t listId) {
  auto& s = editSession();
  const core::TaskList* l = core::findList(app::state().lists, listId);
  s.draft = l ? *l : core::TaskList{};
  s.isNew = false;
  s.dirty = false;
}

void beginNewList() {
  auto& s = editSession();
  s.draft = core::TaskList{};
  s.draft.name = "New list";
  s.draft.icon = "star";
  s.isNew = true;
  s.dirty = true;
}

namespace {

std::unique_ptr<ui::Screen> makePicker(size_t taskIndex);
std::unique_ptr<ui::Screen> makeNewTask();
std::unique_ptr<ui::Screen> makeListTime();
std::unique_ptr<ui::Screen> makeCelebrationPicker();

// Celebration choices (D27), indexed by core::Celebration.
struct CelebrationInfo {
  const char* name;   // Edit list box
  const char* title;  // picker card
  const char* note;
  const char* thumb;  // 140 px picture
};
constexpr CelebrationInfo kCelebrations[core::kCelebrationCount] = {
    {"Star", "Star", "Big star and rays", "fx_celebrate_a"},
    {"Corgi", "Dancing corgi", "A happy dance", "celeb_corgi"},
    {"Poop", "Dancing poop", "A silly dance", "celeb_poop"},
};

const char* celebrationName(core::Celebration c) { return kCelebrations[int(c)].name; }

ui::Button backButton(int id) {
  return {Rect{0, 12, 120, 84}, [](ui::Gfx& g, int x, int y, bool p) { ui::drawBackButton(g, x + 24, y + 16, p); },
          id};
}

std::string timeLabel(const core::TaskList& l) {
  if (l.anyTime()) return "Any time";
  return ui::shortTime(l.fromMin) + "\xE2\x80\x93" + ui::shortTime(l.toMin);
}

// Picture tile used by the pickers: white with a black border; selected = inverted. A caption
// that is too wide for the tile wraps onto two lines at the space nearest the middle (D26).
void drawPictureTile(ui::Gfx& g, int x, int y, int w, int h, int iconSize, const char* icon, const char* label,
                     bool selected, bool pressed) {
  constexpr auto kFont = fonts::Id::atkinson_18b;
  constexpr int kLineH = 20, kGap = 6;
  const bool dark = selected || pressed;
  const int ink = dark ? kWhite : kInk, bg = dark ? kInk : kWhite;
  ui::roundBox(g, x, y, w, h, 16, dark ? 3 : 2, kInk, bg);
  std::string lines[2];
  int nLines = 0;
  if (label) {
    lines[nLines++] = label;
    if (ui::textWidth(g, kFont, lines[0]) > w - 12) {
      const std::string s = label;
      size_t best = std::string::npos;
      for (size_t i = s.find(' '); i != std::string::npos; i = s.find(' ', i + 1)) {
        if (best == std::string::npos || std::abs(int(i) - int(s.size() / 2)) < std::abs(int(best) - int(s.size() / 2))) {
          best = i;
        }
      }
      if (best != std::string::npos) {
        lines[0] = s.substr(0, best);
        lines[1] = s.substr(best + 1);
        nLines = 2;
      }
    }
  }
  const int blockH = iconSize + (nLines ? kGap + nLines * kLineH : 0);
  const int iconY = y + (h - blockH) / 2;
  ui::drawIcon(g, icon, iconSize, x + (w - iconSize) / 2, iconY, ink, bg);
  for (int i = 0; i < nLines; ++i) {
    ui::drawText(g, kFont, lines[i], x + w / 2, iconY + iconSize + kGap + i * kLineH + kLineH / 2,
                 lgfx::middle_center, ink);
  }
}

// ---- picture group tabs (Everyday / Home & chores, D26) ----------------------------------------
constexpr int kTabsH = 62;

// The gray tray behind the two tabs.
void drawTabsTray(ui::Gfx& g, int x, int y) { ui::roundBox(g, x, y, 492, kTabsH, 16, 0, kTile, kTile); }

void drawGroupTab(ui::Gfx& g, int x, int y, int w, int h, core::IconGroup grp, bool active, bool pressed) {
  const int bg = pressed ? kInk : (active ? kWhite : kTile), ink = pressed ? kWhite : kInk;
  if (pressed || active) {
    ui::roundBox(g, x, y, w, h, 12, 3, kInk, bg);
  } else {
    g.fillRect(x, y, w, h, gray(kTile));
  }
  // No "NEW" pill on Home & chores (user, 2026-09-28).
  ui::drawText(g, fonts::Id::atkinson_20b, core::groupName(grp), x + w / 2, y + h / 2, lgfx::middle_center, ink);
}

// Adds the two tab buttons (ids idBase + group) for a tray at (24, y); *active is read at draw time.
void addGroupTabs(std::vector<ui::Button>& out, int y, const core::IconGroup* active, int idBase) {
  for (int i = 0; i < core::kIconGroupCount; ++i) {
    const auto grp = core::IconGroup(i);
    const Rect r{int16_t(24 + 5 + i * 244), int16_t(y + 5), 238, 52};
    out.push_back({r, [grp, active](ui::Gfx& g, int x, int y2, bool p) {
                     drawGroupTab(g, x, y2, 238, 52, grp, *active == grp, p);
                   },
                   idBase + i});
  }
}

// Indices into core::kTaskIcons of one group, in catalog order.
std::vector<int> iconsIn(core::IconGroup grp) {
  std::vector<int> out;
  for (int i = 0; i < core::kTaskIconCount; ++i) {
    if (core::kTaskIcons[i].group == grp) out.push_back(i);
  }
  return out;
}

// Redraws `area` (tray + tiles) in one GC16 refresh after a tab change: a whole grid of new pictures
// changes, and GL16 left the screen unclean (D24).
void refreshTabsAndTiles(const Rect& area, int trayY, const std::vector<ui::Button>& buttons) {
  ui::Region r(area);
  drawTabsTray(r.canvas(), 24 - area.x, trayY - area.y);
  for (const auto& b : buttons) {
    if (area.contains(b.rect.x, b.rect.y)) b.draw(r.canvas(), b.rect.x - area.x, b.rect.y - area.y, false);
  }
  r.push(hal::Mode::Quality);
}

// ======================================================================================
// Edit list
// ======================================================================================
class EditList : public AdminScreen {
 public:
  EditList() { build(); }

  void render(lgfx::LovyanGFX& g) override {
    const auto& d = editSession().draft;
    ui::drawText(g, fonts::Id::fredoka_34, "Edit list", 90, 34, lgfx::top_left, kInk);
    ui::drawSectionLabel(g, 24, 100, "LIST NAME");
    ui::drawSectionLabel(g, kTime.x, 100, "TIME");
    ui::drawSectionLabel(g, 24, 196, "LIST PICTURE");
    ui::drawSectionLabel(g, kCelebR.x, 196, "CELEBRATE");
    ui::drawSectionLabel(g, 24, 304, "TASKS");
    char count[48];
    std::snprintf(count, sizeof count, "%u of 8 \xC2\xB7 tap a picture to change it", unsigned(d.tasks.size()));
    ui::drawText(g, fonts::Id::atkinson_18b, count, kScreenW - 24, 304, lgfx::top_right, kText2);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    auto& s = editSession();
    auto& d = s.draft;
    if (id == kBack) {
      if (!s.dirty) {
        ui::screens().show(makeParentSettings());
        return;
      }
      ui::screens().show(makeConfirm(
          "Discard changes?", "Your edits to this list will be lost.", "Discard",
          [] { ui::screens().show(makeParentSettings()); }, [] { ui::screens().show(makeEditList()); }));
    } else if (id == kSave) {
      save();
    } else if (id == kName) {
      TextEntryOptions o;
      o.title = "List name";
      o.text = d.name;
      o.placeholder = "List name";
      o.onDone = [](const std::string& t) {
        if (!t.empty()) {
          editSession().draft.name = t;
          editSession().dirty = true;
        }
        ui::screens().show(makeEditList());
      };
      o.onCancel = [] { ui::screens().show(makeEditList()); };
      ui::screens().show(makeTextEntry(std::move(o)));
    } else if (id == kTimeId) {
      ui::screens().show(makeListTime());
    } else if (id == kAdd) {
      ui::screens().show(makeNewTask());
    } else if (id == kCeleb) {
      ui::screens().show(makeCelebrationPicker());
    } else if (id >= kListIcon && id < kListIcon + core::kListIconCount) {
      d.icon = core::kListIcons[id - kListIcon].key;
      s.dirty = true;
      ui::screens().show(makeEditList());
    } else if (id >= kRemove) {
      d.tasks.erase(d.tasks.begin() + (id - kRemove));
      s.dirty = true;
      ui::screens().show(makeEditList());
    } else if (id >= kDown || id >= kUp) {
      const size_t i = size_t(id - (id >= kDown ? kDown : kUp));
      const size_t j = id >= kDown ? i + 1 : i - 1;
      if (j < d.tasks.size() && !(id < kDown && i == 0)) {
        std::swap(d.tasks[i], d.tasks[j]);
        s.dirty = true;
        ui::screens().show(makeEditList());
      }
    } else if (id >= kTaskName) {
      const size_t i = size_t(id - kTaskName);
      TextEntryOptions o;
      o.title = "Task name";
      o.text = d.tasks[i].label;
      o.placeholder = "e.g. Feed the cat";
      o.suggestions = true;
      o.onDone = [i](const std::string& t) {
        auto& dd = editSession().draft;
        if (!t.empty() && i < dd.tasks.size()) {
          dd.tasks[i].label = t;
          editSession().dirty = true;
        }
        ui::screens().show(makeEditList());
      };
      o.onCancel = [] { ui::screens().show(makeEditList()); };
      ui::screens().show(makeTextEntry(std::move(o)));
    } else if (id >= kTaskIcon) {
      ui::screens().show(makePicker(size_t(id - kTaskIcon)));
    }
  }

 private:
  enum : int {
    kBack = 1, kSave, kName, kTimeId, kAdd, kCeleb,
    kListIcon = 100, kTaskIcon = 1000, kTaskName = 2000, kUp = 3000, kDown = 4000, kRemove = 5000
  };
  static constexpr Rect kSaveR{int16_t(kScreenW - 24 - 110), 28, 110, 52};
  static constexpr Rect kNameR{24, 124, 330, 56};
  static constexpr Rect kTime{368, 124, 148, 56};
  static constexpr Rect kCelebR{396, 220, 120, 64};  // right of the 5 list pictures (D27)

  void build() {
    const auto& d = editSession().draft;
    buttons_.push_back(backButton(kBack));
    buttons_.push_back({kSaveR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kSaveR.w, kSaveR.h}, "Save",
                                             fonts::Id::atkinson_22b, true, p);
                        },
                        kSave});
    buttons_.push_back({kNameR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawTextBox(g, Rect{int16_t(x), int16_t(y), kNameR.w, kNameR.h},
                                          editSession().draft.name, "List name", fonts::Id::atkinson_26b, 2, p);
                        },
                        kName});
    buttons_.push_back({kTime, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawTextBox(g, Rect{int16_t(x), int16_t(y), kTime.w, kTime.h},
                                          timeLabel(editSession().draft), "", fonts::Id::atkinson_20b, 2, p);
                        },
                        kTimeId});
    for (int i = 0; i < core::kListIconCount; ++i) {
      const char* key = core::kListIcons[i].key;
      buttons_.push_back({Rect{int16_t(24 + i * 74), 220, 64, 64}, [key](ui::Gfx& g, int x, int y, bool p) {
                            const bool sel = editSession().draft.icon == key;
                            const bool dark = sel || p;
                            ui::roundBox(g, x, y, 64, 64, 16, 3, kInk, dark ? kInk : kWhite);
                            ui::drawIcon(g, key, 40, x + 12, y + 12, dark ? kWhite : kInk, dark ? kInk : kWhite);
                          },
                          kListIcon + i});
    }
    buttons_.push_back({kCelebR, [](ui::Gfx& g, int x, int y, bool p) {
                          const int fill = p ? kInk : kWhite, ink = p ? kWhite : kInk;
                          ui::roundBox(g, x, y, kCelebR.w, kCelebR.h, 16, 3, kInk, fill);
                          ui::drawText(g, fonts::Id::atkinson_22b, celebrationName(editSession().draft.celebration),
                                       x + 14, y + kCelebR.h / 2, lgfx::middle_left, ink);
                          ui::drawIcon(g, "ui_chevron_right", 32, x + kCelebR.w - 8 - 32, y + (kCelebR.h - 32) / 2,
                                       ink, fill);
                        },
                        kCeleb});
    int y = 332;
    for (size_t i = 0; i < d.tasks.size(); ++i) {
      const int ry = y;
      // Row frame (not a button): drawn with the icon button.
      buttons_.push_back({Rect{24, int16_t(ry), 64, 64}, [i](ui::Gfx& g, int x, int y2, bool p) {
                            ui::roundBox(g, x, y2, 492, 64, 16, 2, kInk, kWhite);  // row frame
                            g.fillSmoothRoundRect(x + 6, y2 + 6, 52, 52, 12, gray(p ? kInk : kTile));
                            const auto& t = editSession().draft.tasks[i];
                            ui::drawIcon(g, t.icon.c_str(), 36, x + 14, y2 + 14, p ? kWhite : kInk, p ? kInk : kTile);
                          },
                          kTaskIcon + int(i)});
      buttons_.push_back({Rect{90, int16_t(ry + 8), 264, 48}, [i](ui::Gfx& g, int x, int y2, bool p) {
                            ui::roundBox(g, x, y2, 264, 48, 10, 2, p ? kInk : kDivider, p ? kInk : kWhite);
                            ui::drawText(g, fonts::Id::atkinson_20b, editSession().draft.tasks[i].label, x + 10,
                                         y2 + 24, lgfx::middle_left, p ? kWhite : kInk);
                          },
                          kTaskName + int(i)});
      const char* icons[3] = {"ui_up", "ui_down", "ui_trash"};
      const int ids[3] = {kUp, kDown, kRemove};
      for (int k = 0; k < 3; ++k) {
        const char* ic = icons[k];
        const int lvl = k == 2 ? kInk : kBorder2;
        buttons_.push_back({Rect{int16_t(362 + k * 52), int16_t(ry + 10), 44, 44},
                            [ic, lvl](ui::Gfx& g, int x, int y2, bool p) { ui::drawSmallIconButton(g, x, y2, ic, lvl, p); },
                            ids[k] + int(i)});
      }
      y += 72;
    }
    if (d.tasks.size() < core::kMaxTasks) {
      const Rect add{24, int16_t(y), 492, 60};
      buttons_.push_back({add, [](ui::Gfx& g, int x, int y2, bool p) {
                            ui::drawAddButton(g, Rect{int16_t(x), int16_t(y2), 492, 60}, "Add a task", p);
                          },
                          kAdd});
    }
  }

  void save() {
    auto& s = editSession();
    auto& set = app::state().lists;
    auto& progress = app::state().progress;
    core::TaskList d = s.draft;
    d.name = core::trimmed(d.name);
    if (d.name.empty()) d.name = "List";
    for (auto& t : d.tasks) {
      if (t.id == 0) t.id = set.nextId++;  // new tasks get their ids now
    }
    if (s.isNew) {
      if (set.lists.size() >= core::kMaxLists) return;
      d.id = set.nextId++;
      set.lists.push_back(d);
    } else {
      core::TaskList* old = core::findList(set, d.id);
      if (!old) return;
      // Forget progress for tasks that were removed (stars already earned today are kept).
      for (const auto& t : old->tasks) {
        const bool kept = std::any_of(d.tasks.begin(), d.tasks.end(), [&](const core::Task& n) { return n.id == t.id; });
        if (!kept) progress.done[d.id].erase(t.id);
      }
      *old = d;
    }
    app::saveLists();
    app::saveProgress();
    s.dirty = false;
    ui::screens().show(makeParentSettings());
  }

  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// Change picture (existing task, D26 mockup): old -> new preview, Everyday / Home & chores tabs,
// 4-column grid with captions. A tap selects; "Use this picture" applies; Back leaves it unchanged.
// ======================================================================================
class Picker : public AdminScreen {
 public:
  explicit Picker(size_t taskIndex) : index_(taskIndex) {
    const auto& tasks = editSession().draft.tasks;
    if (index_ < tasks.size()) {
      label_ = tasks[index_].label;
      current_ = core::taskIconIndex(tasks[index_].icon);
    }
    sel_ = current_;
    if (sel_ >= 0) group_ = core::kTaskIcons[sel_].group;
    preview_ = {kNewTile, [this](ui::Gfx& g, int x, int y, bool) { drawNewTile(g, x, y); }, kPreviewId};
    build();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, "Change picture", 90, 34, lgfx::top_left, kInk);
    drawCard(g);
    drawTabsTray(g, 24, kTrayY);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id == kBack) {
      ui::screens().show(makeEditList());
    } else if (id == kUse) {
      auto& tasks = editSession().draft.tasks;
      if (index_ < tasks.size() && sel_ >= 0 && sel_ != current_) {
        tasks[index_].icon = core::kTaskIcons[sel_].key;
        editSession().dirty = true;
      }
      ui::screens().show(makeEditList());
    } else if (id >= kTab) {
      const auto grp = core::IconGroup(id - kTab);
      press_.clearPending();  // the tab is redrawn with the grid below
      if (grp == group_) {
        gridDirty_ = true;  // repaint the pressed tab
        return;
      }
      group_ = grp;
      build();
      gridDirty_ = true;
    } else if (id != sel_) {  // a tile: the tapped one is restored as selected by the press feedback
      const int old = sel_;
      sel_ = id;
      for (const auto& b : buttons_) {
        if (b.id == old) press_.queueRestore(b);
      }
      press_.queueRestore(preview_);
    }
  }

  void onTick(uint32_t nowMs) override {
    if (gridDirty_ && !hal::epd().busy()) {
      gridDirty_ = false;
      refreshTabsAndTiles(kGridArea, kTrayY, buttons_);
      return;
    }
    AdminScreen::onTick(nowMs);
  }

 private:
  enum : int { kBack = 100, kUse = 101, kPreviewId = 102, kTab = 200 };
  static constexpr Rect kCard{24, 98, 492, 88};
  static constexpr Rect kOldTile{42, 112, 60, 60};
  static constexpr Rect kNewTile{158, 112, 60, 60};
  static constexpr int kTrayY = 204, kGridY = 284, kTileW = 117, kTileH = 110;
  static constexpr Rect kGridArea{24, kTrayY, 492, kGridY + 4 * (kTileH + 10) - 10 - kTrayY};
  static constexpr Rect kUseR{24, 860, 492, 76};

  void build() {
    buttons_.clear();
    buttons_.push_back(backButton(kBack));
    buttons_.push_back({kUseR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kUseR.w, kUseR.h}, "Use this picture",
                                             fonts::Id::atkinson_26b, true, p);
                        },
                        kUse});
    addGroupTabs(buttons_, kTrayY, &group_, kTab);
    const auto icons = iconsIn(group_);
    for (size_t k = 0; k < icons.size(); ++k) {
      const int i = icons[k];
      const Rect r{int16_t(24 + (k % 4) * (kTileW + 8)), int16_t(kGridY + (k / 4) * (kTileH + 10)), kTileW, kTileH};
      buttons_.push_back({r, [this, i](ui::Gfx& g, int x, int y, bool p) {
                            const auto& info = core::kTaskIcons[i];
                            drawPictureTile(g, x, y, kTileW, kTileH, 48, info.key, info.tile, sel_ == i, p);
                          },
                          i});
    }
  }

  // "Wash face -> Wash hands" card: the task's picture now, an arrow, the picture chosen.
  void drawCard(ui::Gfx& g) {
    ui::roundBox(g, kCard.x, kCard.y, kCard.w, kCard.h, 18, 2, kInk, kWhite);
    ui::roundBox(g, kOldTile.x, kOldTile.y, kOldTile.w, kOldTile.h, 14, 0, kTile, kTile);
    if (current_ >= 0) {
      ui::drawIcon(g, core::kTaskIcons[current_].key, 40, kOldTile.x + 10, kOldTile.y + 10, kMuted, kTile);
    }
    ui::drawIcon(g, "ui_arrow_right", 28, kOldTile.x + kOldTile.w + 14, kOldTile.y + 16, kInk, kWhite);
    drawNewTile(g, kNewTile.x, kNewTile.y);
    const int tx = kNewTile.x + kNewTile.w + 14, maxW = kCard.x + kCard.w - 14 - tx;
    ui::drawSectionLabel(g, tx, kCard.y + 16, "TASK");
    const auto font = ui::textWidth(g, fonts::Id::atkinson_26b, label_) <= maxW ? fonts::Id::atkinson_26b
                                                                               : fonts::Id::atkinson_22b;
    ui::drawText(g, font, label_, tx, kCard.y + 58, lgfx::middle_left, kInk);
  }

  void drawNewTile(ui::Gfx& g, int x, int y) {
    ui::roundBox(g, x, y, kNewTile.w, kNewTile.h, 14, 0, kInk, kInk);
    if (sel_ >= 0) ui::drawIcon(g, core::kTaskIcons[sel_].key, 40, x + 10, y + 10, kWhite, kInk);
  }

  size_t index_;
  std::string label_;
  int current_ = -1;
  int sel_ = -1;
  core::IconGroup group_ = core::IconGroup::Everyday;
  bool gridDirty_ = false;
  ui::Button preview_;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// New task: type a name and pick a picture (DESIGN_SPEC §7.4, keyboard §7.5)
// ======================================================================================
class NewTask : public AdminScreen {
 public:
  NewTask() {
    field_.maxLen = core::kMaxLabelLen;
    kb_.setCaps(false, true, false);
    buildButtons();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_30, "New task", 24, 38, lgfx::top_left, kInk);
    ui::drawSectionLabel(g, 24, 100, "1 \xC2\xB7 TYPE THE TASK");
    drawField(g, 0, 0);
    ui::drawSectionLabel(g, 24, 226, "2 \xC2\xB7 CHOOSE A PICTURE");
    for (const auto& b : buttons_) {
      if (b.id != kFieldId) b.draw(g, b.rect.x, b.rect.y, false);
    }
    if (typing_) kb_.draw(g, 0, 0);
  }

  void onTouch(const hal::TouchEvent& e) override {
    if (typing_ && ui::Keyboard::kRect.contains(e.x, e.y)) {
      handleKey(kb_.onTouch(e));
      return;
    }
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id == kCancelId) {
      ui::screens().show(makeEditList());
    } else if (id == kFieldId) {
      if (!typing_) {
        typing_ = true;
        rebuild();
      }
    } else if (id == kAddId) {
      const auto btn = core::addButton(draft_);
      if (!btn.enabled) return;
      auto& d = editSession().draft;
      if (d.tasks.size() < core::kMaxTasks) {
        d.tasks.push_back(core::Task{0, core::trimmed(draft_.text), draft_.icon});
        editSession().dirty = true;
      }
      ui::screens().show(makeEditList());
    } else if (id >= kTabId) {
      group_ = core::IconGroup(id - kTabId);
      buildButtons();
      press_.clearPending();  // the tab is redrawn with the grid below
      gridDirty_ = true;
    } else if (id < core::kTaskIconCount) {
      core::pickIcon(draft_, core::kTaskIcons[id].key);
      field_.text = draft_.text;
      kb_.setCaps(field_.shift, field_.shift || field_.text.empty(), false);
      rebuild();
    }
  }

  void onTick(uint32_t nowMs) override {
    AdminScreen::onTick(nowMs);
    if (hal::epd().busy()) return;
    if (gridDirty_) {
      gridDirty_ = false;
      const int rows = typing_ ? kTypingRows : 4;
      refreshTabsAndTiles(Rect{24, kTrayY, 492, int16_t(kGridY + rows * 94 - 10 - kTrayY)}, kTrayY, buttons_);
      return;
    }
    if (fieldDirty_) {
      fieldDirty_ = false;
      ui::Region r(kFieldArea);
      drawField(r.canvas(), -kFieldArea.x, -kFieldArea.y);
      r.binarize();
      r.push(hal::Mode::Fast);
      return;
    }
    if (typing_) kb_.tick();
  }

 private:
  enum : int { kCancelId = 100, kFieldId = 101, kAddId = 102, kTabId = 200 };
  static constexpr Rect kCancelR{int16_t(kScreenW - 24 - 120), 30, 120, 48};
  static constexpr Rect kPreview{24, 124, 60, 60};
  static constexpr Rect kField{96, 124, 420, 60};
  static constexpr Rect kFieldArea{0, 120, kScreenW, 96};
  // Tabs, then tiles (84 px, pitch 94); while typing, the 2 rows above the keyboard (y 592).
  static constexpr int kTrayY = 250, kGridY = 322, kTypingRows = 2;
  static constexpr Rect kAddR{24, 712, 492, 72};

  void handleKey(const ui::Keyboard::Action& a) {
    using A = ui::Keyboard::Action::Type;
    switch (a.type) {
      case A::Char: core::typeChar(field_, a.c); textChanged(); break;
      case A::Space: core::typeSpace(field_); textChanged(); break;
      case A::Backspace: core::backspace(field_); textChanged(); break;
      case A::Shift: field_.shift = !field_.shift; kb_.setCaps(field_.shift, field_.shift || field_.text.empty()); break;
      case A::Suggestion: field_.text = a.text.substr(0, field_.maxLen); textChanged(); break;
      case A::Done:
        typing_ = false;
        rebuild();
        break;
      default: break;
    }
  }

  void textChanged() {
    core::setTextByUser(draft_, field_.text);
    kb_.setCaps(field_.shift, field_.shift || field_.text.empty());
    kb_.setSuggestions(core::suggestions(field_.text));
    fieldDirty_ = true;
  }

  void rebuild() {
    buildButtons();
    ui::screens().redraw();
  }

  void buildButtons() {
    buttons_.clear();
    buttons_.push_back({kCancelR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawLightButton(g, Rect{int16_t(x), int16_t(y), kCancelR.w, kCancelR.h}, "Cancel",
                                              fonts::Id::atkinson_20b, p);
                        },
                        kCancelId});
    buttons_.push_back({kField, [](ui::Gfx&, int, int, bool) {}, kFieldId});
    addGroupTabs(buttons_, kTrayY, &group_, kTabId);
    // While typing only the first rows of pictures show above the keyboard.
    const auto icons = iconsIn(group_);
    const size_t shown = std::min(icons.size(), size_t((typing_ ? kTypingRows : 4) * 4));
    for (size_t k = 0; k < shown; ++k) {
      const char* key = core::kTaskIcons[icons[k]].key;
      const Rect r{int16_t(24 + (k % 4) * 125), int16_t(kGridY + (k / 4) * 94), 117, 84};
      buttons_.push_back({r, [this, key](ui::Gfx& g, int x, int y, bool p) {
                            drawPictureTile(g, x, y, 117, 84, 44, key, nullptr, draft_.icon == key, p);
                          },
                          icons[k]});
    }
    if (!typing_) {
      buttons_.push_back({kAddR, [this](ui::Gfx& g, int x, int y, bool p) {
                            const auto btn = core::addButton(draft_);
                            ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kAddR.w, kAddR.h}, btn.label.c_str(),
                                               fonts::Id::atkinson_26b, btn.enabled, p && btn.enabled);
                          },
                          kAddId});
    }
  }

  void drawField(ui::Gfx& g, int ox, int oy) {
    // Preview tile: dashed until a picture is chosen.
    const int tx = kPreview.x + ox, ty = kPreview.y + oy;
    if (draft_.icon.empty()) {
      ui::dashedRoundBox(g, tx, ty, kPreview.w, kPreview.h, 14, 2, kBorder2);
    } else {
      ui::roundBox(g, tx, ty, kPreview.w, kPreview.h, 14, 0, kInk, kTile);
      ui::drawIcon(g, draft_.icon.c_str(), 44, tx + 8, ty + 8, kInk, kTile);
    }
    const Rect r{int16_t(kField.x + ox), int16_t(kField.y + oy), kField.w, kField.h};
    ui::roundBox(g, r.x, r.y, r.w, r.h, 14, typing_ ? 4 : 3, kInk, kWhite);
    int caretX = r.x + 16;
    if (field_.text.empty()) {
      ui::drawText(g, fonts::Id::atkinson_26b, "e.g. Feed the cat", r.x + 16, r.y + r.h / 2, lgfx::middle_left, kDash);
    } else {
      ui::drawText(g, fonts::Id::atkinson_26b, field_.text, r.x + 16, r.y + r.h / 2, lgfx::middle_left, kInk);
      caretX += ui::textWidth(g, fonts::Id::atkinson_26b, field_.text) + 3;
    }
    if (typing_) g.fillRect(caretX, r.y + (r.h - 32) / 2, 3, 32, gray(kInk));
    char count[64];
    std::snprintf(count, sizeof count, "%u / 18 letters \xC2\xB7 short words read best", unsigned(field_.text.size()));
    ui::drawText(g, fonts::Id::atkinson_18b, count, 24 + ox, 194 + oy, lgfx::top_left, kText2);
  }

  core::NewTaskDraft draft_;
  core::TextField field_;
  ui::Keyboard kb_;
  bool typing_ = true;
  bool fieldDirty_ = false;
  bool gridDirty_ = false;
  core::IconGroup group_ = core::IconGroup::Everyday;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// List time (drives the NOW pill, Q2)
// ======================================================================================
class ListTime : public AdminScreen {
 public:
  ListTime() {
    const auto& d = editSession().draft;
    any_ = d.anyTime();
    from_ = d.anyTime() ? 7 * 60 : d.fromMin;
    to_ = d.anyTime() ? 8 * 60 : d.toMin;
    buildButtons();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_30, "List time", 24, 38, lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_22b, "The list shows NOW during this time.", 24, 110, lgfx::top_left, kText2);
    ui::drawText(g, fonts::Id::atkinson_22b, "Any time", 24, 190, lgfx::middle_left, kInk);
    if (!any_) {
      ui::drawText(g, fonts::Id::atkinson_22b, "From", 24, 290, lgfx::middle_left, kInk);
      ui::drawText(g, fonts::Id::atkinson_22b, "To", 24, 380, lgfx::middle_left, kInk);
      ui::drawText(g, fonts::Id::atkinson_26b, ui::shortTime(from_), kValX, 290, lgfx::middle_center, kInk);
      ui::drawText(g, fonts::Id::atkinson_26b, ui::shortTime(to_), kValX, 380, lgfx::middle_center, kInk);
    }
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    auto step = [](int m, int d) { return ((m + d) % 1440 + 1440) % 1440; };
    switch (id) {
      case 1: ui::screens().show(makeEditList()); return;
      case 2: {
        auto& d = editSession().draft;
        d.fromMin = any_ ? -1 : from_;
        d.toMin = any_ ? -1 : to_;
        editSession().dirty = true;
        ui::screens().show(makeEditList());
        return;
      }
      case 3:  // the steppers appear or disappear: redraw the screen
        any_ = !any_;
        buildButtons();
        ui::screens().redraw();
        return;
      case 4: from_ = step(from_, -30); break;
      case 5: from_ = step(from_, +30); break;
      case 6: to_ = step(to_, -30); break;
      case 7: to_ = step(to_, +30); break;
      default: return;
    }
    // Only the two values change: refresh just them (M7).
    ui::Region r(kValues);
    r.canvas().setTextColor(gray(kInk));
    ui::drawText(r.canvas(), fonts::Id::atkinson_26b, ui::shortTime(from_), kValX - kValues.x, 290 - kValues.y,
                 lgfx::middle_center, kInk);
    ui::drawText(r.canvas(), fonts::Id::atkinson_26b, ui::shortTime(to_), kValX - kValues.x, 380 - kValues.y,
                 lgfx::middle_center, kInk);
    r.binarize();
    r.push(hal::Mode::Fast);
  }

 private:
  static constexpr int kValX = kScreenW - 24 - 44 - 12 - 50;
  static constexpr Rect kValues{int16_t(kValX - 50), 262, 100, 146};
  static constexpr Rect kCancelR{int16_t(kScreenW - 24 - 120), 30, 120, 48};
  static constexpr Rect kDoneR{24, 820, 492, 72};

  void buildButtons() {
    buttons_.clear();
    buttons_.push_back({kCancelR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawLightButton(g, Rect{int16_t(x), int16_t(y), kCancelR.w, kCancelR.h}, "Cancel",
                                              fonts::Id::atkinson_20b, p);
                        },
                        1});
    buttons_.push_back({kDoneR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kDoneR.w, kDoneR.h}, "Done",
                                             fonts::Id::atkinson_26b, true, p);
                        },
                        2});
    buttons_.push_back({Rect{int16_t(kScreenW - 24 - 76), 168, 76, 44}, [this](ui::Gfx& g, int x, int y, bool p) {
                          if (p) ui::roundBox(g, x, y, 76, 44, 22, 3, kInk, kInk);
                          else ui::drawToggle(g, x, y, any_, (kScreenW - 24 - 76) - x, 168 - y);
                        },
                        3, true});
    if (any_) return;
    auto stepBtn = [](const char* label) {
      return [label](ui::Gfx& g, int x, int y, bool p) {
        ui::roundBox(g, x, y, 44, 44, 12, 2, p ? kInk : kBorder2, p ? kInk : kWhite);
        ui::drawText(g, fonts::Id::atkinson_26b, label, x + 22, y + 21, lgfx::middle_center, p ? kWhite : kInk);
      };
    };
    const int minusX = kValX - 50 - 12 - 44 + 6, plusX = kScreenW - 24 - 44;
    buttons_.push_back({Rect{int16_t(minusX), 268, 44, 44}, stepBtn("\xE2\x80\x93"), 4});
    buttons_.push_back({Rect{int16_t(plusX), 268, 44, 44}, stepBtn("+"), 5});
    buttons_.push_back({Rect{int16_t(minusX), 358, 44, 44}, stepBtn("\xE2\x80\x93"), 6});
    buttons_.push_back({Rect{int16_t(plusX), 358, 44, 44}, stepBtn("+"), 7});
  }

  bool any_;
  int from_, to_;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// Celebration (D27): which All done screen this list shows. A tap selects a card; "Use this
// celebration" applies it to the draft; Back leaves it unchanged.
// ======================================================================================
class CelebrationPicker : public AdminScreen {
 public:
  CelebrationPicker() : sel_(int(editSession().draft.celebration)) {
    buttons_.push_back(backButton(kBack));
    buttons_.push_back({kUseR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kUseR.w, kUseR.h},
                                             "Use this celebration", fonts::Id::atkinson_26b, true, p);
                        },
                        kUse});
    for (int i = 0; i < core::kCelebrationCount; ++i) {
      const Rect r{24, int16_t(kCardY + i * (kCardH + 14)), 492, kCardH};
      buttons_.push_back({r, [this, i](ui::Gfx& g, int x, int y, bool p) { drawCard(g, x, y, i, p); }, i});
    }
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, "Celebration", 90, 34, lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_22b, "Shown when this list is all done.", 24, 104, lgfx::top_left, kText2);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id == kBack) {
      ui::screens().show(makeEditList());
    } else if (id == kUse) {
      auto& s = editSession();
      if (s.draft.celebration != core::Celebration(sel_)) {
        s.draft.celebration = core::Celebration(sel_);
        s.dirty = true;
      }
      ui::screens().show(makeEditList());
    } else if (id < core::kCelebrationCount && id != sel_) {
      const int old = sel_;
      sel_ = id;  // the tapped card is restored as selected by the press feedback
      for (const auto& b : buttons_) {
        if (b.id == old) press_.queueRestore(b);
      }
    }
  }

 private:
  enum : int { kBack = 100, kUse = 101 };
  static constexpr int kCardY = 150, kCardH = 200;
  static constexpr Rect kUseR{24, 860, 492, 76};

  // Card: 140 px picture, name and note; the chosen one has a 4 px border and a check badge.
  void drawCard(ui::Gfx& g, int x, int y, int i, bool pressed) {
    const auto& info = kCelebrations[i];
    const bool chosen = sel_ == i;
    const int fill = pressed ? kInk : kWhite, ink = pressed ? kWhite : kInk;
    ui::roundBox(g, x, y, 492, kCardH, 20, chosen ? 4 : 2, kInk, fill);
    ui::drawIcon(g, info.thumb, 140, x + 30, y + (kCardH - 140) / 2, ink, fill);
    ui::drawText(g, fonts::Id::atkinson_30b, info.title, x + 196, y + 82, lgfx::middle_left, ink);
    ui::drawText(g, fonts::Id::atkinson_22b, info.note, x + 196, y + 124, lgfx::middle_left, pressed ? kWhite : kText2);
    if (chosen) {
      const int cx = x + 492 - 44, cy = y + 44;
      g.fillSmoothCircle(cx, cy, 24, gray(ink));
      ui::drawIcon(g, "ui_check", 34, cx - 17, cy - 17, fill, ink);
    }
  }

  int sel_;
  std::vector<ui::Button> buttons_;
};

std::unique_ptr<ui::Screen> makePicker(size_t taskIndex) { return std::make_unique<Picker>(taskIndex); }
std::unique_ptr<ui::Screen> makeCelebrationPicker() { return std::make_unique<CelebrationPicker>(); }
std::unique_ptr<ui::Screen> makeNewTask() { return std::make_unique<NewTask>(); }
std::unique_ptr<ui::Screen> makeListTime() { return std::make_unique<ListTime>(); }

}  // namespace

std::unique_ptr<ui::Screen> makeEditList() { return std::make_unique<EditList>(); }

}  // namespace screens
