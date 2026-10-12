// Parent settings, Device settings, Confirm and Text entry screens (DESIGN_SPEC §7.2, D20).
#include <cstdio>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../app/version.h"
#include "../hal/clock.h"
#include "../hal/net.h"
#include "../core/battery.h"
#include "../core/logic_day.h"
#include "../hal/power.h"
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

void AdminScreen::onTick(uint32_t nowMs) {
  constexpr uint32_t kIdleMs = 120000;  // parent area closes itself after 2 minutes
  if (nowMs - hal::touch().lastActivityMs() > kIdleMs) ui::screens().show(makePickList());
  press_.tick();
}

namespace {

void header(ui::Gfx& g, const char* title) {
  ui::drawBackButton(g, 24, 28, false);
  ui::drawText(g, fonts::Id::fredoka_34, title, 90, 34, lgfx::top_left, kInk);
}

ui::Button backButton(int id) {
  return {Rect{0, 12, 120, 84}, [](ui::Gfx& g, int x, int y, bool p) { ui::drawBackButton(g, x + 24, y + 16, p); },
          id};
}

std::string listMeta(const core::TaskList& l) {
  char buf[48];
  const unsigned n = unsigned(l.tasks.size());
  if (l.anyTime()) {
    std::snprintf(buf, sizeof buf, "%u task%s \xC2\xB7 Any time", n, n == 1 ? "" : "s");
  } else {
    std::snprintf(buf, sizeof buf, "%u task%s \xC2\xB7 %s \xE2\x80\x93 %s", n, n == 1 ? "" : "s",
                  ui::shortTime(l.fromMin).c_str(), ui::shortTime(l.toMin).c_str());
  }
  return buf;
}

// ======================================================================================
// Parent settings: the lists (D20: device settings moved to their own screen)
// ======================================================================================
class ParentSettings : public AdminScreen {
 public:
  ParentSettings() { build(); }

  void render(lgfx::LovyanGFX& g) override {
    header(g, "Parent settings");
    ui::drawIcon(g, "ui_lock", 34, kScreenW - 24 - 34, 37, kInk, kWhite);
    const auto& lists = app::state().lists.lists;
    ui::drawSectionLabel(g, 24, 104 + top_, "TASK LISTS");
    char count[16];
    std::snprintf(count, sizeof count, "%u of 6", unsigned(lists.size()));
    ui::drawText(g, fonts::Id::atkinson_18b, count, kScreenW - 24, 104 + top_, lgfx::top_right, kText2);
    for (const auto& b : buttons_) {
      if (b.id != kBack) b.draw(g, b.rect.x, b.rect.y, false);
    }
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    auto& set = app::state().lists;
    if (id == kBack) {
      ui::screens().show(makePickList());
    } else if (id == kAdd) {
      beginNewList();
      ui::screens().show(makeEditList());
    } else if (id == kDevice) {
      ui::screens().show(makeDeviceSettings());
    } else if (id == kClockBanner) {
      ui::screens().show(makeDateTime());
    } else if (id >= kTrash) {
      const auto& l = set.lists[id - kTrash];
      const uint32_t listId = l.id;
      ui::screens().show(makeConfirm(
          "Delete list?", "\xE2\x80\x9C" + l.name + "\xE2\x80\x9D and its tasks will be removed.", "Delete",
          [listId] {
            core::removeList(app::state().lists, app::state().progress, listId);
            app::saveLists();
            app::saveProgress();
            ui::screens().show(makeParentSettings());
          },
          [] { ui::screens().show(makeParentSettings()); }));
    } else if (id >= kDown || id >= kUp) {
      const bool down = id >= kDown;
      const auto& l = set.lists[id - (down ? kDown : kUp)];
      if (core::moveList(set, l.id, down ? +1 : -1) == core::Result::Ok) {
        app::saveLists();
        ui::screens().show(makeParentSettings());
      }
    } else if (id >= kBody) {
      beginEditList(set.lists[id - kBody].id);
      ui::screens().show(makeEditList());
    }
  }

 private:
  enum : int { kBack = 1, kAdd = 2, kDevice = 3, kClockBanner = 4, kBody = 1000, kUp = 2000, kDown = 3000, kTrash = 4000 };

  void build() {
    buttons_.clear();
    buttons_.push_back(backButton(kBack));
    // TM-02: the clock was never set -> banner above the lists.
    top_ = 0;
    if (!hal::clock::valid()) {
      const Rect banner{24, 100, 492, 56};
      buttons_.push_back({banner, [](ui::Gfx& g, int x, int y2, bool p) {
                            ui::roundBox(g, x, y2, 492, 56, 14, 3, kInk, p ? kWhite : kInk);
                            ui::drawText(g, fonts::Id::atkinson_22b, "The clock is not set. Tap to set it.", x + 16,
                                         y2 + 28, lgfx::middle_left, p ? kInk : kWhite);
                          },
                          kClockBanner});
      top_ = 70;
    }
    const auto& lists = app::state().lists.lists;
    int y = 132 + top_;
    for (size_t i = 0; i < lists.size(); ++i) {
      const core::TaskList l = lists[i];
      const Rect row{24, int16_t(y), 492, 76};
      auto drawRow = [l, row](ui::Gfx& g, int ox, int oy, bool p) {
        const int x = row.x + ox, yy = row.y + oy;
        ui::roundBox(g, x, yy, row.w, row.h, 18, 2, kInk, p ? kInk : kWhite);
        g.fillSmoothRoundRect(x + 12, yy + 10, 56, 56, 14, gray(p ? kWhite : kTile));
        ui::drawIcon(g, l.icon.c_str(), 38, x + 21, yy + 19, kInk, p ? kWhite : kTile);
        ui::drawText(g, fonts::Id::atkinson_26b, l.name, x + 80, yy + 10, lgfx::top_left, p ? kWhite : kInk);
        ui::drawText(g, fonts::Id::atkinson_18b, listMeta(l), x + 80, yy + 44, lgfx::top_left, p ? kWhite : kText2);
      };
      // The row body (tap = edit) draws the whole row; the Region clips it to the body area.
      buttons_.push_back({Rect{row.x, row.y, 324, row.h},
                          [drawRow, row](ui::Gfx& g, int x, int y2, bool p) {
                            drawRow(g, x - row.x, y2 - row.y, p);
                          },
                          kBody + int(i)});
      const int bx = row.x + row.w - 10 - 44;
      const int by = row.y + 16;
      buttons_.push_back({Rect{int16_t(bx - 108), int16_t(by), 44, 44},
                          [](ui::Gfx& g, int x, int y2, bool p) { ui::drawSmallIconButton(g, x, y2, "ui_up", kBorder2, p); },
                          kUp + int(i)});
      buttons_.push_back({Rect{int16_t(bx - 54), int16_t(by), 44, 44},
                          [](ui::Gfx& g, int x, int y2, bool p) { ui::drawSmallIconButton(g, x, y2, "ui_down", kBorder2, p); },
                          kDown + int(i)});
      buttons_.push_back({Rect{int16_t(bx), int16_t(by), 44, 44},
                          [](ui::Gfx& g, int x, int y2, bool p) { ui::drawSmallIconButton(g, x, y2, "ui_trash", kInk, p); },
                          kTrash + int(i)});
      y += 86;
    }
    if (lists.size() < core::kMaxLists) {
      const Rect add{24, int16_t(y), 492, 64};
      buttons_.push_back({add, [](ui::Gfx& g, int x, int y2, bool p) {
                            ui::drawAddButton(g, Rect{int16_t(x), int16_t(y2), 492, 64}, "Add a list", p);
                          },
                          kAdd});
      y += 74;
    }
    const Rect dev{24, int16_t(y + 22), 492, 64};
    buttons_.push_back({dev, [](ui::Gfx& g, int x, int y2, bool p) {
                          ui::roundBox(g, x, y2, 492, 64, 18, 2, kInk, p ? kInk : kWhite);
                          ui::drawText(g, fonts::Id::atkinson_22b, "Device settings", x + 16, y2 + 32,
                                       lgfx::middle_left, p ? kWhite : kInk);
                          ui::drawIcon(g, "ui_chevron_right", 32, x + 492 - 16 - 32, y2 + 16, p ? kWhite : kInk,
                                       p ? kInk : kWhite);
                        },
                        kDevice});
  }

  std::vector<ui::Button> buttons_;
  int top_ = 0;
};

// ======================================================================================
// Device settings (D20)
// ======================================================================================
class DeviceSettings : public AdminScreen {
 public:
  DeviceSettings() { build(); }

  void render(lgfx::LovyanGFX& g) override {
    header(g, "Device settings");
    ui::drawSectionLabel(g, 24, 104, "DEVICE");
    ui::roundBox(g, kCard.x, kCard.y, kCard.w, kCard.h, 18, 2, kInk, kWhite);
    for (int i = 1; i < kRows; ++i) g.fillRect(kCard.x + 2, kCard.y + i * kRowH, kCard.w - 4, 1, gray(kDivider));
    const char* labels[] = {"Child's name", "Reset lists daily at", "Show weekly stars", "Sleep after"};
    for (int i = 0; i < 4; ++i) {
      ui::drawText(g, fonts::Id::atkinson_20b, labels[i], kCard.x + 16, rowY(i) + kRowH / 2, lgfx::middle_left, kInk);
    }
    ui::drawText(g, fonts::Id::atkinson_20b, "Keep screen up to date", kCard.x + 16, rowY(kRefreshRow) + kRowH / 2,
                 lgfx::middle_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_20b, "2 min idle", kCard.x + kCard.w - 16, rowY(3) + kRowH / 2,
                 lgfx::middle_right, kText2);
    drawValue(g, 0, 0);
    drawToggleArea(g, kToggle, app::state().settings.weeklyStars, 0, 0);
    drawToggleArea(g, kRefreshToggle, app::state().settings.autoRefresh, 0, 0);
    for (const auto& b : buttons_) {
      if (b.id != kBack) b.draw(g, b.rect.x, b.rect.y, false);
    }
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    auto& s = app::state().settings;
    switch (id) {
      case kBack: ui::screens().show(makeParentSettings()); break;
      case kName: {
        TextEntryOptions o;
        o.title = "Child's name";
        o.text = s.childName;
        o.placeholder = "Name";
        o.maxLen = 12;
        o.onDone = [](const std::string& t) {
          app::state().settings.childName = t;
          app::saveSettings();
          ui::screens().show(makeDeviceSettings());
        };
        o.onCancel = [] { ui::screens().show(makeDeviceSettings()); };
        ui::screens().show(makeTextEntry(std::move(o)));
        break;
      }
      case kHourDown:
      case kHourUp:
        s.resetHour = core::stepHour(s.resetHour, id == kHourUp ? +1 : -1);
        app::saveSettings();
        app::rolloverIfNeeded();
        pushValue();
        break;
      case kStars:
        s.weeklyStars = !s.weeklyStars;
        app::saveSettings();
        pushToggle(kToggle, s.weeklyStars);
        break;
      case kRefresh:  // D30: wake from sleep when Pick a list would change
        s.autoRefresh = !s.autoRefresh;
        app::saveSettings();
        pushToggle(kRefreshToggle, s.autoRefresh);
        break;
      case kPin: ui::screens().show(makeChangePin()); break;
      case kClock: ui::screens().show(makeDateTime()); break;
      case kWifi: ui::screens().show(makeWifi()); break;
      case kAbout: ui::screens().show(makeAbout()); break;
      default: break;
    }
  }

 private:
  enum : int { kBack = 1, kName, kHourDown, kHourUp, kStars, kClock, kWifi, kPin, kAbout, kRefresh };
  static constexpr int kRows = 9;
  static constexpr int kRowH = 68;
  static constexpr int kRefreshRow = 6;  // after WiFi, where the mockup has "Refresh screen" (D30)
  static constexpr Rect kCard{24, 132, 492, kRows * kRowH};
  static int rowY(int i) { return kCard.y + i * kRowH; }
  static constexpr int kStepX = kCard.x + kCard.w - 16 - ui::kStepperWidth;
  static constexpr Rect kValue{int16_t(kStepX + 56), int16_t(132 + kRowH + 12), 76, 44};
  static constexpr Rect kToggle{int16_t(24 + 492 - 16 - 76), int16_t(132 + 2 * kRowH + 12), 76, 44};
  static constexpr Rect kRefreshToggle{int16_t(24 + 492 - 16 - 76), int16_t(132 + kRefreshRow * kRowH + 12), 76, 44};

  void drawValue(ui::Gfx& g, int ox, int oy) {
    char v[8];
    std::snprintf(v, sizeof v, "%02d:00", app::state().settings.resetHour);
    g.fillRect(kValue.x + ox, kValue.y + oy, kValue.w, kValue.h, gray(kWhite));
    ui::drawText(g, fonts::Id::atkinson_26b, v, kValue.x + kValue.w / 2 + ox, kValue.y + kValue.h / 2 + oy,
                 lgfx::middle_center, kInk);
  }
  static void drawToggleArea(ui::Gfx& g, const Rect& t, bool on, int ox, int oy) {
    g.fillRect(t.x + ox, t.y + oy, t.w, t.h, gray(kWhite));
    ui::drawToggle(g, t.x + ox, t.y + oy, on, -ox, -oy);
  }
  void pushValue() {
    ui::Region r(kValue);
    drawValue(r.canvas(), -kValue.x, -kValue.y);
    r.push(hal::Mode::Text);
  }
  void pushToggle(const Rect& t, bool on) {  // pure black/white: DU, no blink (M6)
    ui::Region r(t);
    drawToggleArea(r.canvas(), t, on, -t.x, -t.y);
    r.binarize();
    r.push(hal::Mode::Fast);
  }

  void build() {
    buttons_.clear();
    buttons_.push_back(backButton(kBack));
    const Rect name{int16_t(kCard.x + kCard.w - 16 - 180), int16_t(rowY(0) + 12), 180, 44};
    buttons_.push_back({name, [name](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawTextBox(g, Rect{int16_t(x), int16_t(y), name.w, name.h},
                                          app::state().settings.childName, "Name", fonts::Id::atkinson_20b, 2, p);
                        },
                        kName});
    auto stepBtn = [](const char* label) {
      return [label](ui::Gfx& g, int x, int y, bool p) {
        ui::roundBox(g, x, y, 44, 44, 12, 2, p ? kInk : kBorder2, p ? kInk : kWhite);
        ui::drawText(g, fonts::Id::atkinson_26b, label, x + 22, y + 21, lgfx::middle_center, p ? kWhite : kInk);
      };
    };
    buttons_.push_back({Rect{int16_t(kStepX), int16_t(rowY(1) + 12), 44, 44}, stepBtn("\xE2\x80\x93"), kHourDown});
    buttons_.push_back({Rect{int16_t(kStepX + 44 + 12 + 76 + 12), int16_t(rowY(1) + 12), 44, 44}, stepBtn("+"),
                        kHourUp});
    buttons_.push_back({kToggle, [](ui::Gfx& g, int x, int y, bool p) {
                          if (p) {
                            ui::roundBox(g, x, y, 76, 44, 22, 3, kInk, kInk);
                          } else {
                            ui::drawToggle(g, x, y, app::state().settings.weeklyStars, kToggle.x - x, kToggle.y - y);
                          }
                        },
                        kStars, true});
    buttons_.push_back({kRefreshToggle, [](ui::Gfx& g, int x, int y, bool p) {
                          if (p) {
                            ui::roundBox(g, x, y, 76, 44, 22, 3, kInk, kInk);
                          } else {
                            ui::drawToggle(g, x, y, app::state().settings.autoRefresh, kRefreshToggle.x - x,
                                           kRefreshToggle.y - y);
                          }
                        },
                        kRefresh, true});
    // Chevron rows: Date & time and WiFi (M7), Change PIN.
    auto chevron = [](const char* label, std::function<std::string()> value) {
      return [label, value](ui::Gfx& g, int x, int y, bool p) {
        if (p) g.fillRect(x + 2, y + 1, 492 - 4, kRowH - 3, gray(kInk));
        ui::drawText(g, fonts::Id::atkinson_20b, label, x + 16, y + kRowH / 2, lgfx::middle_left, p ? kWhite : kInk);
        const std::string v = value ? value() : "";
        if (!v.empty()) {
          ui::drawText(g, fonts::Id::atkinson_20b, v, x + 492 - 56, y + kRowH / 2, lgfx::middle_right,
                       p ? kWhite : kText2);
        }
        ui::drawIcon(g, "ui_chevron_right", 32, x + 492 - 16 - 32, y + (kRowH - 32) / 2, p ? kWhite : kInk,
                     p ? kInk : kWhite);
      };
    };
    buttons_.push_back({Rect{kCard.x, int16_t(rowY(4)), kCard.w, kRowH},
                        chevron("Date & time", [] { return hal::clock::valid() ? std::string() : std::string("not set"); }),
                        kClock});
    buttons_.push_back({Rect{kCard.x, int16_t(rowY(5)), kCard.w, kRowH},
                        chevron("WiFi & time sync", [] {
                          const std::string s = hal::net::savedSsid();
                          return s.empty() ? std::string("off") : s.substr(0, 14);
                        }),
                        kWifi});
    buttons_.push_back({Rect{kCard.x, int16_t(rowY(7)), kCard.w, kRowH}, chevron("Change PIN", nullptr), kPin});
    buttons_.push_back({Rect{kCard.x, int16_t(rowY(8)), kCard.w, kRowH},
                        chevron("About Task Pal", [] { return std::string("v") + app::kVersion; }), kAbout});
  }

  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// About (D29, docs/About.dc.html): logo, name, version, then Version / Author / Device rows.
// ======================================================================================
class About : public AdminScreen {
 public:
  About() { buttons_.push_back(backButton(kBack)); }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawBackButton(g, 24, 28, false);
    ui::drawText(g, fonts::Id::fredoka_34, "About", 90, 34, lgfx::top_left, kInk);
    // Logo: the 148 px tile starts 20 px into the 168 px picture (the star badge sticks out above).
    ui::drawIcon(g, "about_logo", 168, (kScreenW - 148) / 2, 150 - 20, kInk, kWhite);
    ui::drawText(g, fonts::Id::fredoka_52, "Task Pal", kScreenW / 2, 322, lgfx::top_center, kInk);
    const std::string version = std::string("Version ") + app::kVersion;
    ui::drawText(g, fonts::Id::atkinson_22b, version, kScreenW / 2, 392, lgfx::top_center, kText2);
    // Info card.
    ui::roundBox(g, kCard.x, kCard.y, kCard.w, kCard.h, 18, 2, kInk, kWhite);
    const std::string battery = batteryText();
    shownBattery_ = battery;
    lastBatteryMs_ = millis();
    const char* labels[kRows] = {"Version", "Author", "Device", "Battery"};
    const char* values[kRows] = {app::kVersion, app::kAuthor, app::kDevice, battery.c_str()};
    for (int i = 0; i < kRows; ++i) {
      const int y = kCard.y + i * kRowH;
      if (i > 0) g.fillRect(kCard.x + 2, y, kCard.w - 4, 1, gray(kDivider));
      ui::drawText(g, fonts::Id::atkinson_20b, labels[i], kCard.x + 16, y + kRowH / 2, lgfx::middle_left, kInk);
      ui::drawText(g, fonts::Id::atkinson_20b, values[i], kCard.x + kCard.w - 16, y + kRowH / 2, lgfx::middle_right,
                   kText2);
    }
  }

  void onTouch(const hal::TouchEvent& e) override {
    if (press_.handle(e, buttons_) == kBack) ui::screens().show(makeDeviceSettings());
  }

 private:
  // "62% · 3.84 V" (D31). The M5Paper V1.1 has no charge-status signal, and the battery still reads
  // above 4.25 V for a while after unplugging, so no "Charging" guess: just the level, capped at 100%.
  static std::string batteryText() {
    const int mv = hal::power::batteryMvMedian(15);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d%% \xC2\xB7 %d.%02d V", core::batteryPercent(mv), mv / 1000, (mv % 1000) / 10);
    return buf;
  }

  void onTick(uint32_t nowMs) override {
    AdminScreen::onTick(nowMs);
    // Keep the battery row current while the screen is open (e.g. just after unplugging).
    if (nowMs - lastBatteryMs_ < kBatteryEveryMs || hal::epd().busy()) return;
    lastBatteryMs_ = nowMs;
    const std::string text = batteryText();
    if (text == shownBattery_) return;
    shownBattery_ = text;
    ui::Region r(kBatteryValue);
    ui::drawText(r.canvas(), fonts::Id::atkinson_20b, text, kBatteryValue.w, kBatteryValue.h / 2, lgfx::middle_right,
                 kText2);
    r.push(hal::Mode::Text);
  }

  enum : int { kBack = 1 };
  static constexpr int kRows = 4;
  static constexpr uint32_t kBatteryEveryMs = 10000;
  static constexpr int kRowH = 68;
  static constexpr Rect kCard{24, 466, 492, kRows * kRowH};
  // Value area of the Battery row (right-aligned at the card's inner edge).
  static constexpr Rect kBatteryValue{int16_t(24 + 492 - 16 - 240), int16_t(466 + 3 * kRowH + 12), 240, 44};
  uint32_t lastBatteryMs_ = 0;
  std::string shownBattery_;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// Confirm
// ======================================================================================
class Confirm : public AdminScreen {
 public:
  Confirm(std::string title, std::string message, std::string yes, std::function<void()> onYes,
          std::function<void()> onNo)
      : title_(std::move(title)), message_(std::move(message)), yes_(std::move(yes)), onYes_(std::move(onYes)),
        onNo_(std::move(onNo)) {
    buttons_.push_back({kYes, [this](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kYes.w, kYes.h}, yes_.c_str(),
                                             fonts::Id::atkinson_26b, true, p);
                        },
                        1});
    buttons_.push_back({kNo, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawLightButton(g, Rect{int16_t(x), int16_t(y), kNo.w, kNo.h}, "Cancel",
                                              fonts::Id::atkinson_26b, p);
                        },
                        2});
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, title_, kScreenW / 2, 300, lgfx::middle_center, kInk);
    ui::drawText(g, fonts::Id::atkinson_22b, message_, kScreenW / 2, 370, lgfx::middle_center, kText2);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id == 1 && onYes_) onYes_();
    if (id == 2 && onNo_) onNo_();
  }

 private:
  static constexpr Rect kYes{24, 460, 492, 72};
  static constexpr Rect kNo{24, 552, 492, 64};
  std::string title_, message_, yes_;
  std::function<void()> onYes_, onNo_;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// Text entry (child's name, list name, task name)
// ======================================================================================
class TextEntry : public AdminScreen {
 public:
  explicit TextEntry(TextEntryOptions o) : o_(std::move(o)), kb_(o_.password) {
    field_.text = o_.text;
    field_.maxLen = o_.maxLen;
    field_.autoCap = !o_.password;
    field_.freeSpaces = o_.password;
    buttons_.push_back({kCancel, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawLightButton(g, Rect{int16_t(x), int16_t(y), kCancel.w, kCancel.h}, "Cancel",
                                              fonts::Id::atkinson_20b, p);
                        },
                        1});
    kb_.setCaps(false, field_.autoCap && field_.text.empty(), false);
    refreshSuggestions();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_30, o_.title, 24, 38, lgfx::top_left, kInk);
    buttons_[0].draw(g, kCancel.x, kCancel.y, false);
    drawField(g, 0, 0);
    kb_.draw(g, 0, 0);
  }

  void onTouch(const hal::TouchEvent& e) override {
    if (press_.handle(e, buttons_) == 1) {
      if (o_.onCancel) o_.onCancel();
      return;
    }
    using A = ui::Keyboard::Action::Type;
    const auto a = kb_.onTouch(e);
    switch (a.type) {
      case A::Char: core::typeChar(field_, a.c); changed(); break;
      case A::Space: core::typeSpace(field_); changed(); break;
      case A::Backspace: core::backspace(field_); changed(); break;
      case A::Shift: field_.shift = !field_.shift; changed(); break;
      case A::Suggestion:
        field_.text = a.text.substr(0, field_.maxLen);
        changed();
        break;
      case A::Done:
        if (o_.onDone) o_.onDone(core::trimmed(field_.text));
        break;
      default: break;
    }
  }

  void onTick(uint32_t nowMs) override {
    AdminScreen::onTick(nowMs);
    if (hal::epd().busy()) return;
    if (fieldDirty_) {  // typing is buffered: one field refresh catches up with all keys (ADM-14)
      fieldDirty_ = false;
      ui::Region r(kFieldArea);
      drawField(r.canvas(), -kFieldArea.x, -kFieldArea.y);
      r.binarize();
      r.push(hal::Mode::Fast);
      return;
    }
    kb_.tick();
  }

 private:
  static constexpr Rect kCancel{int16_t(kScreenW - 24 - 120), 30, 120, 48};
  static constexpr Rect kField{24, 104, 492, 64};
  static constexpr Rect kFieldArea{0, 100, kScreenW, 110};

  void changed() {
    kb_.setCaps(field_.shift, field_.shift || (field_.autoCap && field_.text.empty()));
    refreshSuggestions();
    fieldDirty_ = true;
  }
  void refreshSuggestions() {
    if (o_.suggestions) kb_.setSuggestions(core::suggestions(field_.text));
  }

  void drawField(ui::Gfx& g, int ox, int oy) {
    const Rect r{int16_t(kField.x + ox), int16_t(kField.y + oy), kField.w, kField.h};
    ui::roundBox(g, r.x, r.y, r.w, r.h, 14, 4, kInk, kWhite);
    int caretX = r.x + 16;
    if (field_.text.empty()) {
      ui::drawText(g, fonts::Id::atkinson_26b, o_.placeholder ? o_.placeholder : "", r.x + 16, r.y + r.h / 2,
                   lgfx::middle_left, kDash);
    } else {
      ui::drawText(g, fonts::Id::atkinson_26b, field_.text, r.x + 16, r.y + r.h / 2, lgfx::middle_left, kInk);
      caretX += ui::textWidth(g, fonts::Id::atkinson_26b, field_.text) + 3;
    }
    g.fillRect(caretX, r.y + (r.h - 32) / 2, 3, 32, gray(kInk));
    char count[64];
    std::snprintf(count, sizeof count, "%u / %u letters%s", unsigned(field_.text.size()), unsigned(field_.maxLen),
                  o_.suggestions ? " \xC2\xB7 short words read best" : "");
    ui::drawText(g, fonts::Id::atkinson_18b, count, 24 + ox, 180 + oy, lgfx::top_left, kText2);
  }

  TextEntryOptions o_;
  core::TextField field_;
  ui::Keyboard kb_;
  std::vector<ui::Button> buttons_;
  bool fieldDirty_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makeParentSettings() { return std::make_unique<ParentSettings>(); }
std::unique_ptr<ui::Screen> makeDeviceSettings() { return std::make_unique<DeviceSettings>(); }
std::unique_ptr<ui::Screen> makeAbout() { return std::make_unique<About>(); }
std::unique_ptr<ui::Screen> makeConfirm(std::string title, std::string message, std::string yesLabel,
                                        std::function<void()> onYes, std::function<void()> onNo) {
  return std::make_unique<Confirm>(std::move(title), std::move(message), std::move(yesLabel), std::move(onYes),
                                   std::move(onNo));
}
std::unique_ptr<ui::Screen> makeTextEntry(TextEntryOptions opts) { return std::make_unique<TextEntry>(std::move(opts)); }

}  // namespace screens
