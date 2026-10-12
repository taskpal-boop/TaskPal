// Parent PIN screen (DESIGN_SPEC §7.1, D9): enter, first-run create, change. 4 digits, salted
// SHA-256, 5 wrong tries lock entry for 60 s (the guard lives in RTC memory: survives sleep).
#include <esp_random.h>
#include <esp_system.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../app/app.h"
#include "../core/pin.h"
#include "../hal/clock.h"
#include "../ui/controls.h"
#include "../ui/fonts.h"
#include "../ui/icons.h"
#include "../ui/region.h"
#include "../ui/theme.h"
#include "admin.h"
#include "kid.h"

namespace screens {

namespace {

using namespace theme;
using hal::Rect;

RTC_DATA_ATTR core::PinGuard gGuard;

enum class Mode { Enter, Create, CreateConfirm, ChangeOld, ChangeNew, ChangeConfirm };

constexpr Rect kDots{0, 210, kScreenW, 60};
constexpr Rect kMessage{0, 290, kScreenW, 60};
constexpr int kKeyW = 140, kKeyH = 96, kKeyGap = 16;
constexpr int kPadX = (kScreenW - (3 * kKeyW + 2 * kKeyGap)) / 2;
constexpr int kPadY = 400;

class PinScreen : public AdminScreen {
 public:
  PinScreen(Mode mode, std::string first = {}, std::string message = {})
      : mode_(mode), first_(std::move(first)), message_(std::move(message)) {
    buildButtons();
  }

  void render(lgfx::LovyanGFX& g) override {
    ui::drawBackButton(g, 24, 28, false);
    ui::drawText(g, fonts::Id::fredoka_34, "Parent PIN", 90, 34, lgfx::top_left, kInk);
    ui::drawIcon(g, "ui_lock", 34, kScreenW - 24 - 34, 37, kInk, kWhite);
    ui::drawText(g, fonts::Id::atkinson_26b, prompt(), kScreenW / 2, 150, lgfx::middle_center, kInk);
    drawDots(g, 0, 0, false);
    drawMessage(g, 0, 0);
    for (size_t i = 1; i < buttons_.size(); ++i) buttons_[i].draw(g, buttons_[i].rect.x, buttons_[i].rect.y, false);
  }

  void onTouch(const hal::TouchEvent& e) override {
    const int id = press_.handle(e, buttons_);
    if (id < 0) return;
    if (id == kBackId) {
      if (mode_ >= Mode::ChangeOld) ui::screens().show(makeDeviceSettings());
      else ui::screens().show(makePickList());
      return;
    }
    if (lockedSeconds() > 0) return;
    if (id == kDeleteId) {
      if (!entered_.empty()) {
        entered_.pop_back();
        updateDots(hal::Mode::Fast);  // removes black: DU
      }
      return;
    }
    if (entered_.size() >= 4) return;
    entered_.push_back(char('0' + id));
    updateDots(hal::Mode::Fastest);  // adds black: DU4
    if (entered_.size() == 4) complete();
  }

  void onTick(uint32_t nowMs) override {
    AdminScreen::onTick(nowMs);
    // While locked, count the seconds down in the message line.
    const int64_t left = lockedSeconds();
    if (left != lastLeft_ && nowMs - lastMsgMs_ > 1000 && !hal::epd().busy()) {
      lastLeft_ = left;
      lastMsgMs_ = nowMs;
      if (left > 0) {
        char s[64];
        std::snprintf(s, sizeof s, "Too many tries. Wait %d s.", int(left));
        setMessage(s);
      } else if (wasLocked_) {
        wasLocked_ = false;
        setMessage("Try again.");
      }
    }
  }

 private:
  static constexpr int kBackId = 100;
  static constexpr int kDeleteId = 10;

  void buildButtons() {
    buttons_.clear();
    buttons_.push_back({Rect{0, 12, 120, 84}, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::drawBackButton(g, x + 24, y + 16, p);
                        },
                        kBackId});
    for (int d = 0; d <= 9; ++d) {
      const int idx = d == 0 ? 10 : d - 1;  // 1..9, then 0 in the middle of the last row
      const int col = idx % 3, row = idx / 3;
      const Rect r{int16_t(kPadX + col * (kKeyW + kKeyGap)), int16_t(kPadY + row * (kKeyH + kKeyGap)), kKeyW, kKeyH};
      const std::string label(1, char('0' + d));
      buttons_.push_back({r, [label](ui::Gfx& g, int x, int y, bool p) {
                            ui::roundBox(g, x, y, kKeyW, kKeyH, 18, 3, kInk, p ? kInk : kWhite);
                            ui::drawText(g, fonts::Id::atkinson_34b, label, x + kKeyW / 2, y + kKeyH / 2,
                                         lgfx::middle_center, p ? kWhite : kInk);
                          },
                          d});
    }
    const Rect del{int16_t(kPadX + 2 * (kKeyW + kKeyGap)), int16_t(kPadY + 3 * (kKeyH + kKeyGap)), kKeyW, kKeyH};
    buttons_.push_back({del, [](ui::Gfx& g, int x, int y, bool p) {
                          ui::roundBox(g, x, y, kKeyW, kKeyH, 18, 3, kInk, p ? kInk : kWhite);
                          ui::drawIcon(g, "ui_backspace", 32, x + (kKeyW - 32) / 2, y + (kKeyH - 32) / 2,
                                       p ? kWhite : kInk, p ? kInk : kWhite);
                        },
                        kDeleteId});
  }

  const char* prompt() const {
    switch (mode_) {
      case Mode::Enter: return "Enter your PIN";
      case Mode::Create: return "Create a 4-digit PIN";
      case Mode::CreateConfirm: return "Enter it again";
      case Mode::ChangeOld: return "Enter your current PIN";
      case Mode::ChangeNew: return "Choose a new PIN";
      case Mode::ChangeConfirm: return "Enter the new PIN again";
    }
    return "";
  }

  int64_t lockedSeconds() const {
    const int64_t now = hal::clock::epoch();
    return gGuard.lockedUntil > now ? gGuard.lockedUntil - now : 0;
  }

  void drawDots(ui::Gfx& g, int ox, int oy, bool inverted) {
    const int cy = kDots.y + kDots.h / 2 + oy;
    for (int i = 0; i < 4; ++i) {
      const int cx = kScreenW / 2 - 84 + i * 56 + ox;
      const bool filled = inverted || i < int(entered_.size());
      g.fillSmoothCircle(cx, cy, 14, gray(kInk));
      if (!filled) g.fillSmoothCircle(cx, cy, 11, gray(kWhite));
    }
  }

  void drawMessage(ui::Gfx& g, int ox, int oy) {
    if (!message_.empty()) {
      ui::drawText(g, fonts::Id::atkinson_22b, message_, kScreenW / 2 + ox, kMessage.y + kMessage.h / 2 + oy,
                   lgfx::middle_center, kInk);
    }
  }

  void updateDots(hal::Mode m, bool inverted = false) {
    ui::Region r(kDots);
    drawDots(r.canvas(), -kDots.x, -kDots.y, inverted);
    r.binarize();
    r.push(m);
  }

  void setMessage(const std::string& text) {
    message_ = text;
    ui::Region r(kMessage);
    drawMessage(r.canvas(), -kMessage.x, -kMessage.y);
    r.binarize();
    r.push(hal::Mode::Fast);
  }

  void wrong(const char* text) {
    // Shake: all dots flash black, then clear.
    hal::epd().waitIdle();
    updateDots(hal::Mode::Fastest, true);
    hal::epd().waitIdle();
    entered_.clear();
    updateDots(hal::Mode::Fast);
    setMessage(text);
  }

  void complete() {
    auto& s = app::state().settings;
    const std::string pin = entered_;
    switch (mode_) {
      case Mode::Enter:
      case Mode::ChangeOld: {
        int64_t retry = 0;
        const auto r = core::verifyPin(gGuard, pin, s.pinSalt, s.pinHash, hal::clock::epoch(), &retry);
        if (r == core::PinResult::Ok) {
          if (mode_ == Mode::Enter) ui::screens().show(makeParentSettings());
          else ui::screens().show(std::make_unique<PinScreen>(Mode::ChangeNew));
        } else if (r == core::PinResult::Locked) {
          wasLocked_ = true;
          lastLeft_ = -1;
          wrong("Too many tries.");
        } else {
          wrong("Wrong PIN. Try again.");
        }
        break;
      }
      case Mode::Create:
      case Mode::ChangeNew:
        ui::screens().show(std::make_unique<PinScreen>(
            mode_ == Mode::Create ? Mode::CreateConfirm : Mode::ChangeConfirm, pin));
        break;
      case Mode::CreateConfirm:
      case Mode::ChangeConfirm:
        if (pin != first_) {
          ui::screens().show(std::make_unique<PinScreen>(mode_ == Mode::CreateConfirm ? Mode::Create : Mode::ChangeNew,
                                                         std::string{}, "The PINs didn't match. Try again."));
          return;
        }
        {
          uint8_t salt[16];
          esp_fill_random(salt, sizeof salt);
          s.pinSalt = core::toHex(salt, sizeof salt);
          s.pinHash = core::hashPin(pin, s.pinSalt);
          app::saveSettings();
        }
        if (mode_ == Mode::CreateConfirm) ui::screens().show(makeParentSettings());
        else ui::screens().show(makeDeviceSettings());
        break;
    }
  }

  Mode mode_;
  std::string first_;
  std::string message_;
  std::string entered_;
  std::vector<ui::Button> buttons_;
  int64_t lastLeft_ = -1;
  uint32_t lastMsgMs_ = 0;
  bool wasLocked_ = false;
};

}  // namespace

std::unique_ptr<ui::Screen> makeParentEntry() {
  const bool hasPin = !app::state().settings.pinHash.empty();
  return std::make_unique<PinScreen>(hasPin ? Mode::Enter : Mode::Create);
}

std::unique_ptr<ui::Screen> makeChangePin() { return std::make_unique<PinScreen>(Mode::ChangeOld); }

}  // namespace screens
