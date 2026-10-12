// Parent area (DESIGN_SPEC §7): PIN, Parent settings, Device settings, Edit list and the
// text-entry / confirm / picker screens they use. Every admin screen returns to the kid screen
// after 2 minutes without a touch.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/model.h"
#include "../ui/controls.h"
#include "../ui/screen.h"

namespace screens {

// ---- entry ---------------------------------------------------------------------------------
std::unique_ptr<ui::Screen> makeParentEntry();  // PIN screen: create on first use, else enter
std::unique_ptr<ui::Screen> makeParentSettings();
std::unique_ptr<ui::Screen> makeDeviceSettings();
std::unique_ptr<ui::Screen> makeChangePin();
std::unique_ptr<ui::Screen> makeDateTime();   // M7
std::unique_ptr<ui::Screen> makeWifi();       // M7
std::unique_ptr<ui::Screen> makeAbout();      // D29

// ---- Edit list (works on a draft, saved only on "Save") -----------------------------------
struct EditSession {
  core::TaskList draft;
  bool isNew = false;
  bool dirty = false;
};
EditSession& editSession();
void beginEditList(uint32_t listId);  // copies the list into the session
void beginNewList();
std::unique_ptr<ui::Screen> makeEditList();

// ---- shared helper screens -------------------------------------------------------------------
struct TextEntryOptions {
  std::string title;
  std::string text;
  const char* placeholder = "";
  size_t maxLen = 18;
  bool suggestions = false;  // task-name suggestions (DESIGN_SPEC §7.5)
  bool password = false;     // no auto-capital, free spaces, #+= symbols (WiFi, M7)
  std::function<void(const std::string&)> onDone;  // then show the next screen yourself
  std::function<void()> onCancel;
};
std::unique_ptr<ui::Screen> makeTextEntry(TextEntryOptions opts);

std::unique_ptr<ui::Screen> makeConfirm(std::string title, std::string message, std::string yesLabel,
                                        std::function<void()> onYes, std::function<void()> onNo);

// Base for admin screens: 2-minute idle timeout back to the kid screen, and the shared button
// press feedback (subclasses that override onTick must call AdminScreen::onTick).
class AdminScreen : public ui::Screen {
 public:
  void onTick(uint32_t nowMs) override;
  bool isParentArea() const override { return true; }

 protected:
  ui::PressFeedback press_;
};

}  // namespace screens
