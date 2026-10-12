// M1 sleep spike: SP-06 (deep vs light sleep, touch wake latency, timer wake, image retention).
// v2: persistent boot log in NVS, IT8951 health probe + recovery, side-button (G38) wake.
// Build/flash:  pio run -e spike_sleep -t upload
// Operated by touch (works on battery). Serial 115200: 'L' prints the boot log, 'X' clears it.
#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {

auto& D = M5.Display;
constexpr int W = 540;
constexpr int H = 960;
constexpr gpio_num_t kMainPower = GPIO_NUM_2;  // M5Paper main power hold (M5EPD_MAIN_PWR_PIN)
constexpr gpio_num_t kEpdReset = GPIO_NUM_23;  // IT8951 reset (M5GFX cfg.pin_rst)
constexpr gpio_num_t kSideButton = GPIO_NUM_38;  // side switch push, active low

RTC_DATA_ATTR uint32_t wakeCount = 0;       // survives deep sleep, lost on power loss / reset
RTC_DATA_ATTR uint32_t lastSleepMode = 0;   // 1 = deep, 2 = deep + timer
RTC_DATA_ATTR uint8_t restartsForEpd = 0;   // guards the esp_restart() recovery loop

// ---- persistent boot log (NVS) -----------------------------------------------------
struct BootRecord {
  uint8_t cause;        // esp_sleep_wakeup_cause_t
  uint8_t recovery;     // 0 ok, 1 fixed by EPD reset+init, 2 still bad (restarting)
  uint16_t battery_mv;
  uint16_t begin_ms;    // M5.begin() done
  uint16_t probe_ms;    // first tiny draw (4 s = IT8951 busy timeout)
  uint16_t drawn_ms;    // full screen displayed
  uint16_t wake_no;     // wakeCount (0 after power loss / reset)
  uint16_t on_usb;      // 1 if USB power detected
};
constexpr int kLogSize = 12;
Preferences prefs;

void logBoot(const BootRecord& r) {
  prefs.begin("sp06", false);
  uint32_t n = prefs.getUInt("n", 0);
  char key[8];
  snprintf(key, sizeof key, "r%u", (unsigned)(n % kLogSize));
  prefs.putBytes(key, &r, sizeof r);
  prefs.putUInt("n", n + 1);
  prefs.end();
}

const char* causeName(uint8_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_EXT0: return "touch";
    case ESP_SLEEP_WAKEUP_EXT1: return "side button";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "power-on/reset";
    default: return "other";
  }
}

void printLog() {
  prefs.begin("sp06", true);
  uint32_t n = prefs.getUInt("n", 0);
  Serial.printf("[log] %u boots recorded (showing last %d)\n", (unsigned)n, kLogSize);
  for (uint32_t i = (n > kLogSize ? n - kLogSize : 0); i < n; ++i) {
    char key[8];
    snprintf(key, sizeof key, "r%u", (unsigned)(i % kLogSize));
    BootRecord r{};
    prefs.getBytes(key, &r, sizeof r);
    Serial.printf("[log] #%u cause=%-14s wake#=%u usb=%u batt=%umV begin=%ums probe=%ums drawn=%ums epd=%s\n",
                  (unsigned)i, causeName(r.cause), r.wake_no, r.on_usb, r.battery_mv, r.begin_ms, r.probe_ms,
                  r.drawn_ms, r.recovery == 0 ? "ok" : (r.recovery == 1 ? "RECOVERED" : "FAILED->restart"));
  }
  prefs.end();
}

// ---- IT8951 health ----------------------------------------------------------------------
// A hung IT8951 never releases BUSY, so every write waits for M5GFX's 4 s timeout.
uint32_t probeEpd() {
  uint32_t t0 = millis();
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillRect(0, 0, 4, 4, TFT_WHITE);
  return millis() - t0;
}

bool epdHealthy(uint32_t probeMs) { return probeMs < 1500; }

void resetEpd() {
  pinMode(kEpdReset, OUTPUT);
  digitalWrite(kEpdReset, LOW);
  delay(50);
  digitalWrite(kEpdReset, HIGH);
  delay(200);
  D.init();
  D.setRotation(0);
  D.setAutoDisplay(false);
}

// ---- UI ---------------------------------------------------------------------------------
struct Button { int x, y, w, h; const char* label; };
const Button kDeep = {40, 430, 460, 110, "Deep sleep"};
const Button kLight = {40, 570, 460, 110, "Light sleep"};
const Button kTimer = {40, 710, 460, 110, "Deep sleep + 20 s timer"};

void drawButton(const Button& b) {
  D.fillRoundRect(b.x, b.y, b.w, b.h, 22, TFT_BLACK);
  D.fillRoundRect(b.x + 4, b.y + 4, b.w - 8, b.h - 8, 18, TFT_WHITE);
  D.setTextDatum(middle_center);
  D.setFont(&fonts::FreeSansBold18pt7b);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
}

bool hit(const Button& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void drawMain(const char* line1, const char* line2) {
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.setTextDatum(top_left);
  D.setFont(&fonts::FreeSansBold18pt7b);
  D.drawString("SP-06 sleep test v2", 40, 40);
  D.setFont(&fonts::FreeSans12pt7b);
  D.drawString(line1, 40, 110);
  D.drawString(line2, 40, 150);
  D.drawString(("Wake #" + String(wakeCount) + "   battery " + String(M5.Power.getBatteryVoltage()) +
                " mV").c_str(), 40, 190);
  D.drawString("Asleep: the image stays. Wake it with", 40, 260);
  D.drawString("a tap or the side button (push).", 40, 292);
  drawButton(kDeep);
  drawButton(kLight);
  drawButton(kTimer);
  D.display();
  D.waitDisplay();
}

void showStatus(const char* text) {
  D.setEpdMode(epd_mode_t::epd_fastest);
  D.fillRect(0, 850, W, 110, TFT_WHITE);
  D.setTextDatum(middle_center);
  D.setFont(&fonts::FreeSansBold12pt7b);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.drawString(text, W / 2, 900);
  D.display(0, 850, W, 110);
  D.waitDisplay();
}

void goDeepSleep(bool timer) {
  showStatus(timer ? "Deep sleep: wakes by itself in 20 s" : "Deep sleep: tap or side button");
  lastSleepMode = timer ? 2 : 1;
  Serial.printf("[sleep] entering deep sleep%s\n", timer ? " with 20 s timer" : "");
  Serial.flush();
  // Side button as a second wake source (ext1; touch uses ext0 inside M5.Power.deepSleep).
  esp_sleep_enable_ext1_wakeup(1ULL << kSideButton, ESP_EXT1_WAKEUP_ALL_LOW);
  // Keep the main power latch high through deep sleep, or the board turns off on battery.
  gpio_hold_en(kMainPower);
  gpio_deep_sleep_hold_en();
  M5.Power.deepSleep(timer ? 20ULL * 1000 * 1000 : 0, true);
}

void goLightSleep() {
  showStatus("Light sleep: tap to wake");
  Serial.println("[sleep] entering light sleep");
  Serial.flush();
  D.sleep();
  M5.Power.lightSleep(0, true);
  uint32_t t0 = millis();
  D.wakeup();
  ++wakeCount;
  showStatus("Light sleep: woke");
  long ms = millis() - t0;
  Serial.printf("[sleep] light sleep wake -> status redrawn (DU4) in %ld ms\n", ms);
  char line[64];
  snprintf(line, sizeof line, "Light wake: redrawn in %ld ms", ms);
  showStatus(line);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = false;  // keep the image across deep-sleep wakes
  M5.begin(cfg);
  uint32_t tBegin = millis();
  gpio_hold_dis(kMainPower);
  gpio_deep_sleep_hold_dis();
  D.setRotation(0);
  D.setAutoDisplay(false);

  auto cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) ++wakeCount;

  BootRecord rec{};
  rec.cause = (uint8_t)cause;
  rec.wake_no = wakeCount;
  rec.battery_mv = M5.Power.getBatteryVoltage();
  rec.on_usb = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging ? 1 : 0;
  rec.begin_ms = tBegin;

  uint32_t probe = probeEpd();
  rec.probe_ms = probe;
  if (!epdHealthy(probe)) {
    Serial.printf("[sleep] IT8951 probe %u ms -> resetting EPD\n", (unsigned)probe);
    resetEpd();
    probe = probeEpd();
    if (epdHealthy(probe)) {
      rec.recovery = 1;
    } else {
      rec.recovery = 2;
      logBoot(rec);
      if (restartsForEpd < 2) {
        ++restartsForEpd;
        Serial.println("[sleep] IT8951 still hung -> esp_restart()");
        Serial.flush();
        esp_restart();
      }
    }
  }
  restartsForEpd = 0;

  char l1[64], l2[64];
  snprintf(l1, sizeof l1, "Woke by: %s%s", causeName(cause), rec.recovery == 1 ? " (EPD recovered)" : "");
  snprintf(l2, sizeof l2, "M5.begin %lu ms, EPD probe %u ms", (unsigned long)tBegin, (unsigned)rec.probe_ms);
  drawMain(l1, l2);
  rec.drawn_ms = millis();
  logBoot(rec);

  char status[64];
  snprintf(status, sizeof status, "Boot to full screen: %u ms", rec.drawn_ms);
  showStatus(status);
  Serial.println();
  Serial.printf("[sleep] wake #%lu, cause %s, last sleep mode %lu\n", (unsigned long)wakeCount,
                causeName(cause), (unsigned long)lastSleepMode);
  printLog();
}

void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasClicked()) {
    if (hit(kDeep, t.x, t.y)) goDeepSleep(false);
    else if (hit(kTimer, t.x, t.y)) goDeepSleep(true);
    else if (hit(kLight, t.x, t.y)) goLightSleep();
  }
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'L') printLog();
    if (c == 'X') { prefs.begin("sp06", false); prefs.clear(); prefs.end(); Serial.println("[log] cleared"); }
  }
  delay(10);
}
