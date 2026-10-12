// Date & time, Time zone and WiFi & time sync screens (DESIGN_SPEC §7.6-7.7, M7).
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../hal/clock.h"
#include "../hal/net.h"
#include "../ui/controls.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "admin.h"

namespace screens {

using namespace theme;
using hal::Rect;

namespace {

struct Zone {
  const char* name;
  const char* tz;
};
// POSIX TZ rules; the device follows daylight saving automatically.
const Zone kZones[] = {
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},     {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},    {"US Arizona", "MST7"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},     {"Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii", "HST10"},                           {"UK / Ireland", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"India", "IST-5:30"},                         {"Japan", "JST-9"},
    {"Australia East", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"UTC", "UTC0"},
};

const char* zoneName(const std::string& tz) {
  for (const auto& z : kZones) {
    if (tz == z.tz) return z.name;
  }
  return "Custom";
}

std::string formatNow() {
  const time_t t = time(nullptr);
  tm l{};
  localtime_r(&t, &l);
  char buf[48];
  strftime(buf, sizeof buf, "%a %d %b %Y, %H:%M", &l);
  return buf;
}

std::string formatLastSync() {
  const int64_t s = app::state().settings.lastSync;
  if (s <= 0) return "Last synced: never";
  const time_t t = time_t(s);
  tm l{};
  localtime_r(&t, &l);
  char buf[48];
  strftime(buf, sizeof buf, "Last synced: %d %b, %H:%M", &l);
  return buf;
}

ui::Button backTo(int id) {
  return {Rect{0, 12, 120, 84}, [](ui::Gfx& g, int x, int y, bool p) { ui::drawBackButton(g, x + 24, y + 16, p); },
          id};
}

ui::Button chevronRow(const Rect& r, std::function<std::string()> label, std::function<std::string()> value, int id) {
  return {r, [r, label, value](ui::Gfx& g, int x, int y, bool p) {
            ui::roundBox(g, x, y, r.w, r.h, 18, 2, kInk, p ? kInk : kWhite);
            ui::drawText(g, fonts::Id::atkinson_22b, label(), x + 16, y + r.h / 2, lgfx::middle_left, p ? kWhite : kInk);
            const std::string v = value ? value() : "";
            if (!v.empty()) {
              ui::drawText(g, fonts::Id::atkinson_20b, v, x + r.w - 56, y + r.h / 2, lgfx::middle_right,
                           p ? kWhite : kText2);
            }
            ui::drawIcon(g, "ui_chevron_right", 32, x + r.w - 16 - 32, y + (r.h - 32) / 2, p ? kWhite : kInk,
                         p ? kInk : kWhite);
          },
          id};
}

auto stepButton(const char* label) {
  return [label](ui::Gfx& g, int x, int y, bool p) {
    ui::roundBox(g, x, y, 44, 44, 12, 2, p ? kInk : kBorder2, p ? kInk : kWhite);
    ui::drawText(g, fonts::Id::atkinson_26b, label, x + 22, y + 21, lgfx::middle_center, p ? kWhite : kInk);
  };
}

int daysInMonth(int y, int m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : kDays[m - 1];
}

// ======================================================================================
// Time zone list
// ======================================================================================
class ZonePicker : public AdminScreen {
 public:
  ZonePicker() {
    buttons_.push_back(backTo(1000));
    const std::string current = app::state().settings.tz;
    for (size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); ++i) {
      const Zone z = kZones[i];
      const bool sel = current == z.tz;
      const Rect r{24, int16_t(100 + int(i) * 58), 492, 52};
      buttons_.push_back({r, [r, z, sel](ui::Gfx& g, int x, int y, bool p) {
                            const bool dark = sel || p;
                            ui::roundBox(g, x, y, r.w, r.h, 14, 2, kInk, dark ? kInk : kWhite);
                            ui::drawText(g, fonts::Id::atkinson_22b, z.name, x + 16, y + r.h / 2, lgfx::middle_left,
                                         dark ? kWhite : kInk);
                          },
                          int(i)});
    }
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, "Time zone", 90, 34, lgfx::top_left, kInk);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id < 1000) {
      auto& s = app::state().settings;
      s.tz = kZones[id].tz;
      app::saveSettings();
      hal::clock::setTz(s.tz);
      app::rolloverIfNeeded();
    }
    ui::screens().show(makeDateTime());
  }

 private:
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// Date & time (manual set, TM-01)
// ======================================================================================
class DateTime : public AdminScreen {
 public:
  DateTime() {
    t_ = hal::clock::valid() ? hal::clock::now() : core::LocalTime{{2026, 1, 1}, 12, 0};
    buttons_.push_back(backTo(kBack));
    buttons_.push_back(chevronRow(Rect{24, 170, 492, 64}, [] { return std::string("Time zone"); },
                                  [] { return std::string(zoneName(app::state().settings.tz)); }, kZone));
    for (int i = 0; i < 5; ++i) {
      const int y = kRowsY + i * kRowH + 12;
      buttons_.push_back({Rect{int16_t(kMinusX), int16_t(y), 44, 44}, stepButton("\xE2\x80\x93"), kMinus + i});
      buttons_.push_back({Rect{int16_t(kPlusX), int16_t(y), 44, 44}, stepButton("+"), kPlus + i});
    }
    buttons_.push_back({kSetR, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), kSetR.w, kSetR.h}, "Set clock",
                                             fonts::Id::atkinson_26b, true, p);
                        },
                        kSet});
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, "Date & time", 90, 34, lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_22b,
                 hal::clock::valid() ? "Now: " + formatNow() : std::string("The clock is not set yet."), 24, 110,
                 lgfx::top_left, kInk);
    ui::drawText(g, fonts::Id::atkinson_18b, formatLastSync(), 24, 140, lgfx::top_left, kText2);
    ui::drawSectionLabel(g, 24, kRowsY - 24, "SET BY HAND");
    const char* labels[5] = {"Year", "Month", "Day", "Hour", "Minute"};
    for (int i = 0; i < 5; ++i) {
      ui::drawText(g, fonts::Id::atkinson_22b, labels[i], 24, kRowsY + i * kRowH + kRowH / 2, lgfx::middle_left, kInk);
    }
    drawValues(g, 0, 0);
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id == kBack) {
      ui::screens().show(makeDeviceSettings());
      return;
    }
    if (id == kZone) {
      ui::screens().show(std::make_unique<ZonePicker>());
      return;
    }
    if (id == kSet) {
      hal::clock::setLocal(t_);
      app::rolloverIfNeeded();
      ui::screens().show(makeDeviceSettings());
      return;
    }
    const int field = id >= kPlus ? id - kPlus : id - kMinus;
    const int d = id >= kPlus ? +1 : -1;
    switch (field) {
      case 0: t_.date.y = std::max(2025, std::min(2099, t_.date.y + d)); break;
      case 1: t_.date.m = (t_.date.m - 1 + d + 12) % 12 + 1; break;
      case 2: t_.date.d = (t_.date.d - 1 + d + daysInMonth(t_.date.y, t_.date.m)) % daysInMonth(t_.date.y, t_.date.m) + 1; break;
      case 3: t_.hour = (t_.hour + d + 24) % 24; break;
      case 4: t_.minute = (t_.minute + d + 60) % 60; break;
    }
    t_.date.d = std::min(t_.date.d, daysInMonth(t_.date.y, t_.date.m));
    // Only the value column changes (the whole screen refreshed on every tap, user report M7).
    ui::Region r(kValues);
    drawValues(r.canvas(), -kValues.x, -kValues.y);
    r.binarize();
    r.push(hal::Mode::Fast);
  }

 private:
  enum : int { kBack = 1, kZone, kSet, kMinus = 10, kPlus = 20 };
  static constexpr int kRowsY = 300;
  static constexpr int kRowH = 76;
  static constexpr int kPlusX = kScreenW - 24 - 44;
  static constexpr int kMinusX = kPlusX - 44 - 120;
  static constexpr Rect kValues{int16_t(kMinusX + 48), kRowsY, int16_t(kPlusX - kMinusX - 52), 5 * kRowH};

  void drawValues(ui::Gfx& g, int ox, int oy) {
    const int values[5] = {t_.date.y, t_.date.m, t_.date.d, t_.hour, t_.minute};
    for (int i = 0; i < 5; ++i) {
      char v[8];
      std::snprintf(v, sizeof v, i == 0 ? "%d" : "%02d", values[i]);
      ui::drawText(g, fonts::Id::atkinson_26b, v, (kMinusX + 44 + kPlusX) / 2 + ox, kRowsY + i * kRowH + kRowH / 2 + oy,
                   lgfx::middle_center, kInk);
    }
  }
  static constexpr Rect kSetR{24, 810, 492, 72};
  core::LocalTime t_;
  std::vector<ui::Button> buttons_;
};

// ======================================================================================
// WiFi & time sync (TM-03 .. TM-07)
// ======================================================================================
class Wifi : public AdminScreen {
 public:
  enum class Job { None, Scan, SyncSaved, SyncNew };

  explicit Wifi(Job job = Job::None, std::string ssid = {}, std::string pass = {}, std::string status = {})
      : job_(job), ssid_(std::move(ssid)), pass_(std::move(pass)), status_(std::move(status)) {
    if (job_ == Job::Scan) status_ = "Searching for networks\xE2\x80\xA6";
    if (job_ == Job::SyncSaved || job_ == Job::SyncNew) {
      status_ = "Connecting to " + (job_ == Job::SyncNew ? ssid_ : hal::net::savedSsid()) + "\xE2\x80\xA6";
    }
    build();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawText(g, fonts::Id::fredoka_34, "WiFi & time sync", 90, 34, lgfx::top_left, kInk);
    if (showingNetworks_) {
      ui::drawSectionLabel(g, 24, 104, "CHOOSE YOUR WIFI");
    } else {
      const std::string saved = hal::net::savedSsid();
      ui::drawText(g, fonts::Id::atkinson_22b, saved.empty() ? "No WiFi saved." : "Network: " + saved, 24, 110,
                   lgfx::top_left, kInk);
      ui::drawText(g, fonts::Id::atkinson_18b, formatLastSync(), 24, 142, lgfx::top_left, kText2);
      if (!saved.empty()) {
        ui::drawText(g, fonts::Id::atkinson_20b, "Sync every day at the reset hour", 24, 432, lgfx::middle_left, kInk);
      }
    }
    if (!status_.empty()) {
      ui::drawText(g, fonts::Id::atkinson_22b, status_, kScreenW / 2, 880, lgfx::middle_center, kInk);
    }
    for (const auto& b : buttons_) b.draw(g, b.rect.x, b.rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    if (job_ != Job::None) return;  // busy
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    switch (id) {
      case kBack: ui::screens().show(makeDeviceSettings()); break;
      case kFind: ui::screens().show(std::make_unique<Wifi>(Job::Scan)); break;
      case kSyncNow: ui::screens().show(std::make_unique<Wifi>(Job::SyncSaved)); break;
      case kForget:
        hal::net::forget();
        ui::screens().show(std::make_unique<Wifi>(Job::None, "", "", "WiFi forgotten"));
        break;
      case kAuto:
        app::state().settings.autoSync = !app::state().settings.autoSync;
        app::saveSettings();
        ui::screens().redraw();
        break;
      default:
        if (id >= kNet && id < kNet + int(networks_.size())) {
          const std::string ssid = networks_[id - kNet].ssid;
          TextEntryOptions o;
          o.title = "Password for " + ssid;
          o.placeholder = "WiFi password";
          o.maxLen = 63;
          o.password = true;
          o.onDone = [ssid](const std::string& pass) {
            ui::screens().show(std::make_unique<Wifi>(Job::SyncNew, ssid, pass));
          };
          o.onCancel = [] { ui::screens().show(std::make_unique<Wifi>()); };
          ui::screens().show(makeTextEntry(std::move(o)));
        }
        break;
    }
  }

  void onTick(uint32_t nowMs) override {
    AdminScreen::onTick(nowMs);
    if (job_ == Job::None || hal::epd().busy()) return;
    // The "Searching / Connecting" screen is showing: now do the blocking work.
    const Job job = job_;
    job_ = Job::None;
    if (job == Job::Scan) {
      networks_ = hal::net::scan();
      showingNetworks_ = true;
      status_ = networks_.empty() ? "No networks found." : "";
      build();
      ui::screens().redraw();
      return;
    }
    const std::string ssid = job == Job::SyncNew ? ssid_ : hal::net::savedSsid();
    const auto r = job == Job::SyncNew ? hal::net::sync(ssid_, pass_, app::state().settings.tz)
                                       : hal::net::syncSaved(app::state().settings.tz);
    // Joined = the network and password work: keep them even if the time fetch failed (M7).
    if (job == Job::SyncNew && (r == hal::net::SyncResult::Ok || r == hal::net::SyncResult::NoTime)) {
      hal::net::saveCredentials(ssid_, pass_);
    }
    if (r == hal::net::SyncResult::Ok) {
      app::state().settings.lastSync = hal::clock::epoch();
      app::saveSettings();
      app::rolloverIfNeeded();
    }
    ui::screens().show(std::make_unique<Wifi>(Job::None, "", "", hal::net::describe(r)));
  }

 private:
  enum : int { kBack = 1, kFind, kSyncNow, kForget, kAuto, kNet = 100 };

  void build() {
    buttons_.clear();
    buttons_.push_back(backTo(kBack));
    if (job_ != Job::None) return;
    if (showingNetworks_) {
      for (size_t i = 0; i < networks_.size(); ++i) {
        const auto n = networks_[i];
        const Rect r{24, int16_t(134 + int(i) * 70), 492, 60};
        buttons_.push_back({r, [r, n](ui::Gfx& g, int x, int y, bool p) {
                              ui::roundBox(g, x, y, r.w, r.h, 14, 2, kInk, p ? kInk : kWhite);
                              ui::drawText(g, fonts::Id::atkinson_22b, n.ssid, x + 16, y + r.h / 2, lgfx::middle_left,
                                           p ? kWhite : kInk);
                              // Signal and security help explain a failed join (M7: router tests pending, Q15).
                              const std::string info = std::string(hal::net::signalWord(n.rssi)) + " \xC2\xB7 " + n.auth;
                              ui::drawText(g, fonts::Id::atkinson_18b, info, x + r.w - 60, y + r.h / 2,
                                           lgfx::middle_right, p ? kWhite : kText2);
                              if (n.secure) {
                                ui::drawIcon(g, "ui_lock", 34, x + r.w - 16 - 34, y + (r.h - 34) / 2,
                                             p ? kWhite : kInk, p ? kInk : kWhite);
                              }
                            },
                            kNet + int(i)});
      }
      return;
    }
    const bool saved = hal::net::hasCredentials();
    auto dark = [](const Rect& r, const char* label) {
      return [r, label](ui::Gfx& g, int x, int y, bool p) {
        ui::drawDarkButton(g, Rect{int16_t(x), int16_t(y), r.w, r.h}, label, fonts::Id::atkinson_26b, true, p);
      };
    };
    auto light = [](const Rect& r, const char* label) {
      return [r, label](ui::Gfx& g, int x, int y, bool p) {
        ui::drawLightButton(g, Rect{int16_t(x), int16_t(y), r.w, r.h}, label, fonts::Id::atkinson_22b, p);
      };
    };
    if (saved) {
      const Rect sync{24, 190, 492, 72}, change{24, 280, 492, 60}, forgetR{24, 350, 492, 60};
      buttons_.push_back({sync, dark(sync, "Sync now"), kSyncNow});
      buttons_.push_back({change, light(change, "Change network"), kFind});
      buttons_.push_back({forgetR, light(forgetR, "Forget WiFi"), kForget});
      const Rect tog{int16_t(kScreenW - 24 - 76), 410, 76, 44};
      buttons_.push_back({tog, [tog](ui::Gfx& g, int x, int y, bool p) {
                            if (p) ui::roundBox(g, x, y, 76, 44, 22, 3, kInk, kInk);
                            else ui::drawToggle(g, x, y, app::state().settings.autoSync, tog.x - x, tog.y - y);
                          },
                          kAuto, true});
    } else {
      const Rect find{24, 190, 492, 72};
      buttons_.push_back({find, dark(find, "Find networks"), kFind});
    }
  }

  Job job_;
  std::string ssid_, pass_, status_;
  std::vector<hal::net::Network> networks_;
  bool showingNetworks_ = false;
  std::vector<ui::Button> buttons_;
};

}  // namespace

std::unique_ptr<ui::Screen> makeDateTime() { return std::make_unique<DateTime>(); }
std::unique_ptr<ui::Screen> makeWifi() { return std::make_unique<Wifi>(); }

}  // namespace screens
