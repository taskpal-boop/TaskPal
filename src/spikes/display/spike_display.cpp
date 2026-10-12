// M1 display spike: SP-01 (refresh timing), SP-02 (gray ramp), SP-03 (ghosting),
// SP-04 (VLW fonts), SP-05 (icons), SP-07 (touch during refresh), SP-09 (DU blackout).
// Build/flash:  pio run -e spike_display -t upload   (fonts: pio run -e spike_display -t uploadfs)
// Drive it over serial at 115200 with single-letter commands (see printHelp()).
#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>

#include "../../ui/font_ids.h"
#include "../../ui/icons_data.h"

namespace {

auto& D = M5.Display;
constexpr int W = 540;
constexpr int H = 960;

// Exact 4-bit panel level L (0 = black, 15 = white) with no Bayer dither:
// M5GFX maps gray v to (4v + dither[-30..30]) >> 6, so v = 16L + 8 is always L.
uint8_t grayOf(int level) { return uint8_t(16 * level + 8); }
uint32_t colorOf(int level) {
  uint8_t v = grayOf(level);
  return D.color888(v, v, v);
}

// ---- icons: blend a 4bpp coverage mask into a gray8 canvas ------------------
M5Canvas canvas(&M5.Display);

// binary = true for DU/DU4 frames: fast modes dither anything but pure 0/255 (SP-09),
// so thresholding coverage gives crisp 1-bit edges instead of speckle.
void blitIcon(M5Canvas& c, const icons::Bitmap& b, int x, int y, int inkLevel, int bgLevel,
              bool binary = false) {
  const int ink = binary ? (inkLevel < 8 ? 0 : 255) : grayOf(inkLevel);
  const int bg = binary ? (bgLevel < 8 ? 0 : 255) : grayOf(bgLevel);
  const int stride = (b.w + 1) / 2;
  for (int r = 0; r < b.h; ++r) {
    for (int col = 0; col < b.w; ++col) {
      uint8_t byte = b.data[r * stride + col / 2];
      int a = (col & 1) ? (byte & 0x0F) : (byte >> 4);
      if (!a) continue;
      if (binary && a < 8) continue;
      int v = binary ? ink : bg + (ink - bg) * a / 15;
      c.drawPixel(x + col, y + r, c.color888(v, v, v));
    }
  }
}

const icons::Bitmap* icon(const char* name, int size) {
  return icons::find(icons::fromName(name), size);
}

// ---- helpers ----------------------------------------------------------------
uint32_t refresh(int x, int y, int w, int h) {
  uint32_t t0 = micros();
  D.display(x, y, w, h);
  D.waitDisplay();
  return (micros() - t0) / 1000;
}

void deepClean();  // SP-02b section

void clearScreen() {
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  refresh(0, 0, W, H);
}

void title(const char* text) {
  D.setFont(&fonts::FreeSansBold12pt7b);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.setTextDatum(top_left);
  D.drawString(text, 16, 12);
}

bool waitTap(uint32_t timeoutMs = 60000) {
  Serial.println("  (tap the screen or send any key to continue)");
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    M5.update();
    if (M5.Touch.getDetail().wasPressed()) {
      Serial.printf("  -> tapped after %.1f s\n", (millis() - t0) / 1000.0f);
      return true;
    }
    if (Serial.available()) {
      while (Serial.available()) Serial.read();
      Serial.println("  -> continued from serial");
      return true;
    }
    delay(10);
  }
  Serial.println("  -> timed out (no tap)");
  return false;
}

// ---- SP-01: refresh timing ----------------------------------------------------
struct Region { const char* name; int x, y, w, h; };
const Region kRegions[] = {
    {"check 60x60", 400, 200, 60, 60},
    {"stamp 120x120", 370, 170, 120, 120},
    {"row 484x100", 28, 400, 484, 100},
    {"star 280x280", 130, 560, 280, 280},
    {"full 540x960", 0, 0, W, H},
};
struct Mode { const char* name; epd_mode_t m; };
const Mode kModes[] = {
    {"GC16", epd_mode_t::epd_quality},
    {"GL16", epd_mode_t::epd_text},
    {"DU", epd_mode_t::epd_fast},
    {"DU4", epd_mode_t::epd_fastest},
};

void spTiming(int runs) {
  Serial.printf("SP-01 refresh timing, %d runs each (ms: draw / refresh mean / min / max)\n", runs);
  clearScreen();
  for (const auto& r : kRegions) {
    for (const auto& m : kModes) {
      uint32_t sum = 0, mn = UINT32_MAX, mx = 0, drawSum = 0;
      for (int i = 0; i < runs; ++i) {
        D.setEpdMode(m.m);
        uint32_t t0 = micros();
        D.fillRect(r.x, r.y, r.w, r.h, (i & 1) ? TFT_WHITE : TFT_BLACK);
        drawSum += (micros() - t0) / 1000;
        uint32_t t = refresh(r.x, r.y, r.w, r.h);
        sum += t; mn = min(mn, t); mx = max(mx, t);
      }
      Serial.printf("  %-14s %-4s  draw %4u  refresh %4u / %4u / %4u\n", r.name, m.name,
                    drawSum / runs, sum / runs, mn, mx);
    }
    // leave the region white and clean
    D.setEpdMode(epd_mode_t::epd_quality);
    D.fillRect(r.x, r.y, r.w, r.h, TFT_WHITE);
    refresh(r.x, r.y, r.w, r.h);
  }
  Serial.println("SP-01 done");
}

// ---- SP-02: gray ramp + palette swatches ---------------------------------------
struct Token { const char* name; const char* hex; int level; };
const Token kTokens[] = {
    {"C_WHITE", "#ffffff", 15}, {"C_BG", "#f3f2ee", 15},     {"C_TILE", "#e6e4dd", 13},
    {"C_DONE", "#d9d7d0", 11},  {"C_DIVIDER", "#bdbbb4", 9}, {"C_BORDER2", "#9a9a9a", 7},
    {"C_DASH", "#7a7a7a", 6},   {"C_MUTED", "#6a6a6a", 5},   {"C_TEXT2", "#4a4a4a", 3},
    {"C_INK", "#141414", 0},
};

void spGrayRamp() {
  Serial.println("SP-02 gray ramp: photograph the screen in even light");
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  title("SP-02 levels 0-15 | palette tokens");
  D.setFont(&fonts::FreeSans9pt7b);
  D.setTextDatum(middle_left);
  // left column: 16 levels
  for (int l = 0; l < 16; ++l) {
    int y = 50 + l * 56;
    D.fillRect(16, y, 180, 50, colorOf(l));
    D.setTextColor(l < 8 ? TFT_WHITE : TFT_BLACK, colorOf(l));
    D.drawString(("L" + String(l)).c_str(), 26, y + 25);
  }
  // right column: tokens at their proposed level, with a black border
  for (int i = 0; i < 10; ++i) {
    const auto& t = kTokens[i];
    int y = 50 + i * 88;
    D.fillRect(220, y, 150, 80, colorOf(t.level));
    D.drawRect(220, y, 150, 80, TFT_BLACK);
    D.setTextColor(TFT_BLACK, TFT_WHITE);
    D.drawString(t.name, 380, y + 20);
    D.drawString((String(t.hex) + " L" + t.level).c_str(), 380, y + 50);
  }
  uint32_t ms = refresh(0, 0, W, H);
  Serial.printf("SP-02 drawn (GC16 %u ms)\n", ms);
}

// ---- task row mock used by SP-03 / SP-05 -------------------------------------
enum class RowState { Todo, Flash, Done };

void drawRow(int y, RowState s) {
  const int x = 28, w = 484, h = 100;
  canvas.setColorDepth(lgfx::grayscale_8bit);
  canvas.setPsram(true);
  if (!canvas.getBuffer()) canvas.createSprite(w, h);
  int bg = s == RowState::Flash ? 0 : (s == RowState::Done ? 11 : 15);
  int ink = s == RowState::Flash ? 15 : (s == RowState::Done ? 3 : 0);
  canvas.fillSprite(TFT_WHITE);
  canvas.fillSmoothRoundRect(0, 0, w, h, 22, colorOf(0));
  canvas.fillSmoothRoundRect(3, 3, w - 6, h - 6, 19, colorOf(bg));
  if (s == RowState::Todo) canvas.fillSmoothRoundRect(18, 16, 68, 68, 16, colorOf(13));
  if (auto* b = icon("tooth", 48)) blitIcon(canvas, *b, 28, 26, ink, s == RowState::Todo ? 13 : bg);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(colorOf(ink));
  canvas.drawString("Brush teeth", 104, 50);
  int cx = w - 18 - 30, cy = 50;
  if (s == RowState::Done) {
    canvas.fillSmoothCircle(cx, cy, 30, colorOf(0));
    if (auto* b = icon("ui_check", 40)) blitIcon(canvas, *b, cx - 20, cy - 20, 15, 0);
  } else {
    canvas.fillSmoothCircle(cx, cy, 30, colorOf(ink));
    canvas.fillSmoothCircle(cx, cy, 27, colorOf(bg));
  }
  canvas.pushSprite(x, y);
}

// ---- SP-03: ghosting ---------------------------------------------------------------
void spGhosting(epd_mode_t fastMode, const char* name) {
  Serial.printf("SP-03 ghosting with %s\n", name);
  clearScreen();
  title("SP-03 ghosting");
  refresh(0, 0, W, 60);
  const int y = 400;
  int done = 0;
  for (int target : {5, 10, 20, 40}) {
    for (; done < target; ++done) {
      D.setEpdMode(fastMode);
      drawRow(y, (done & 1) ? RowState::Todo : RowState::Flash);
      refresh(28, y, 484, 100);
    }
    D.setEpdMode(epd_mode_t::epd_text);
    drawRow(y, RowState::Done);
    uint32_t t = refresh(28, y, 484, 100);
    Serial.printf("  after %2d %s cycles -> GL16 done state (%u ms). Score ghosting 0-3 now.\n", target,
                  name, t);
    waitTap();
    D.setEpdMode(epd_mode_t::epd_quality);
    drawRow(y, RowState::Done);
    t = refresh(28, y, 484, 100);
    Serial.printf("  same row with GC16 (%u ms). Still ghosting? Score 0-3.\n", t);
    waitTap();
  }
  Serial.println("SP-03 done");
}

// ---- SP-04: fonts -------------------------------------------------------------------
uint8_t* fontData[size_t(fonts::Id::count)] = {};

bool loadFonts() {
  // Fonts are embedded in flash (D15); copy them to PSRAM once.
  size_t total = 0;
  uint32_t t0 = millis();
  for (size_t i = 0; i < size_t(fonts::Id::count); ++i) {
    const auto& b = fonts::kBlobs[i];
    if (!fontData[i]) {
      fontData[i] = (uint8_t*)ps_malloc(b.size());
      memcpy(fontData[i], b.start, b.size());
    }
    total += b.size();
  }
  Serial.printf("  loaded %u KB into PSRAM in %lu ms; PSRAM free %u KB\n", total / 1024, millis() - t0,
                ESP.getFreePsram() / 1024);
  return true;
}

void spFonts() {
  Serial.println("SP-04 fonts");
  if (!loadFonts()) return;
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.setTextDatum(top_left);
  int y = 10;
  const char* samples[] = {"You did it!", "Good morning!", "Morning", "Parent settings", "New task",
                           "Brush teeth", "After school", "1 2 3", "Brush teeth", "Pick your list", "Back to my lists",
                           "Child's name", "TASK LISTS", "4 / 18 letters"};
  for (size_t i = 0; i < size_t(fonts::Id::count); ++i) {
    if (!fontData[i]) continue;
    uint32_t a = micros();
    D.loadFont(fontData[i]);
    uint32_t load = micros() - a;
    a = micros();
    D.drawString(samples[i], 16, y);
    uint32_t draw = micros() - a;
    Serial.printf("  %-28s switch %5u us, draw %6u us, height %d\n", fonts::kNames[i], load, draw,
                  D.fontHeight());
    y += D.fontHeight() + 8;
  }
  D.unloadFont();
  Serial.printf("  full refresh GC16 %u ms\n", refresh(0, 0, W, H));
  Serial.println("SP-04 done");
}

// ---- SP-04b: task label size ----------------------------------------------------------
// Real task rows with the label at 30/34/38/42 px; long labels wrap onto two lines.
void drawLabelRow(int y, fonts::Id font, const char* label) {
  const int w = 484, h = 100, textX = 104, maxW = w - textX - 18 - 60 - 16;
  M5Canvas c(&M5.Display);
  c.setColorDepth(lgfx::grayscale_8bit);
  c.setPsram(true);
  c.createSprite(w, h);
  c.fillSprite(colorOf(15));
  c.fillSmoothRoundRect(0, 0, w, h, 22, colorOf(0));
  c.fillSmoothRoundRect(3, 3, w - 6, h - 6, 19, colorOf(15));
  c.fillSmoothRoundRect(18, 16, 68, 68, 16, colorOf(13));
  if (auto* b = icon(label[0] == 'B' ? "tooth" : "music", 48)) blitIcon(c, *b, 28, 26, 0, 13);
  c.fillSmoothCircle(w - 48, 50, 30, colorOf(0));
  c.fillSmoothCircle(w - 48, 50, 27, colorOf(15));
  c.loadFont(fontData[size_t(font)]);
  c.setTextColor(colorOf(0));
  c.setTextDatum(middle_left);
  String s(label);
  int tw = c.textWidth(s);
  if (tw <= maxW) {
    c.drawString(s, textX, 50);
  } else {
    int cut = s.lastIndexOf(' ');
    while (cut > 0 && c.textWidth(s.substring(0, cut)) > maxW) cut = s.lastIndexOf(' ', cut - 1);
    int lh = c.fontHeight();
    c.drawString(s.substring(0, cut), textX, 50 - lh / 2);
    c.drawString(s.substring(cut + 1), textX, 50 + lh / 2);
  }
  Serial.printf("  %-14s \"%s\" width %d / %d px -> %s\n", fonts::kNames[size_t(font)], label, tw, maxW,
                tw <= maxW ? "1 line" : "2 lines");
  c.unloadFont();
  c.pushSprite(28, y);
  c.deleteSprite();
}

void spLabelSizes() {
  Serial.println("SP-04b task label sizes");
  if (!loadFonts()) return;
  deepClean();
  D.setEpdMode(epd_mode_t::epd_quality);
  const fonts::Id ids[] = {fonts::Id::atkinson_30b, fonts::Id::atkinson_34b, fonts::Id::atkinson_38b,
                           fonts::Id::atkinson_42b};
  const int sizes[] = {30, 34, 38, 42};
  D.loadFont(fontData[size_t(fonts::Id::atkinson_18b)]);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  D.setTextDatum(top_left);
  int y = 8;
  for (int i = 0; i < 4; ++i) {
    D.drawString(("Label " + String(sizes[i]) + " px").c_str(), 28, y);
    y += 22;
    drawLabelRow(y, ids[i], "Brush teeth");
    y += 104;
    drawLabelRow(y, ids[i], "Practice the piano");
    y += 104;
  }
  D.unloadFont();
  Serial.printf("  GC16 %u ms\n", refresh(0, 0, W, H));
  Serial.println("SP-04b done");
}

// ---- SP-05: icons -------------------------------------------------------------------
void spIcons() {
  Serial.println("SP-05 icons");
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  title("SP-05 icons (48 px row, 280 px star)");
  uint32_t a = micros();
  drawRow(80, RowState::Todo);
  uint32_t rowUs = micros() - a;
  drawRow(200, RowState::Done);
  // all task icons at 48 on tiles
  const char* names[] = {"shirt", "tooth", "bowl", "drop", "comb", "backpack", "shoe", "bed",
                         "book", "pencil", "box", "glass", "plant", "clock", "music", "star"};
  M5Canvas tile(&M5.Display);
  tile.setColorDepth(lgfx::grayscale_8bit);
  tile.createSprite(68, 68);
  for (int i = 0; i < 16; ++i) {
    tile.fillSprite(colorOf(15));
    tile.fillSmoothRoundRect(0, 0, 68, 68, 16, colorOf(13));
    if (auto* b = icon(names[i], 48)) blitIcon(tile, *b, 10, 10, 0, 13);
    tile.pushSprite(28 + (i % 6) * 82, 330 + (i / 6) * 82);
  }
  tile.deleteSprite();
  M5Canvas big(&M5.Display);
  big.setColorDepth(lgfx::grayscale_8bit);
  big.setPsram(true);
  big.createSprite(280, 280);
  big.fillSprite(colorOf(15));
  a = micros();
  if (auto* b = icon("fx_celebrate_a", 280)) blitIcon(big, *b, 0, 0, 0, 15);
  uint32_t starUs = micros() - a;
  big.pushSprite(130, 590);
  big.deleteSprite();
  uint32_t ms = refresh(0, 0, W, H);
  Serial.printf("  row compose+push %u us, 280px star blit %u us, GC16 %u ms\n", rowUs, starUs, ms);
  Serial.println("SP-05 done");
}

// ---- SP-09: DU blackout --------------------------------------------------------------
void spBlackout() {
  Serial.println("SP-09 blackout: tap to start");
  clearScreen();
  title("SP-09: tap to start the blackout");
  refresh(0, 0, W, 60);
  waitTap(120000);
  D.setEpdMode(epd_mode_t::epd_fast);
  D.fillScreen(TFT_BLACK);
  M5Canvas big(&M5.Display);
  big.setColorDepth(lgfx::grayscale_8bit);
  big.setPsram(true);
  big.createSprite(280, 280);
  big.fillSprite(TFT_BLACK);  // pure 0: colorOf(0) = 8 dithers to white dots in DU
  if (auto* b = icon("fx_celebrate_a", 280)) blitIcon(big, *b, 0, 0, 15, 0, true);
  big.pushSprite(130, 340);
  big.deleteSprite();
  uint32_t t = refresh(0, 0, W, H);
  Serial.printf("  full-screen DU blackout %u ms\n", t);
  delay(500);
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_WHITE);
  title("SP-09 reveal (GC16 after blackout)");
  t = refresh(0, 0, W, H);
  Serial.printf("  GC16 reveal %u ms. Any leftover ghost of the star?\n", t);
  Serial.println("SP-09 done");
}

// ---- SP-02b: palette in context -----------------------------------------------------
// Real combinations: rows (todo vs done at L11 and L10), keyboard strip, disabled button.
void deepClean() {
  D.setEpdMode(epd_mode_t::epd_quality);
  D.fillScreen(TFT_BLACK);
  refresh(0, 0, W, H);
  D.fillScreen(TFT_WHITE);
  refresh(0, 0, W, H);
}

void drawRowAt(int y, bool done, int doneLevel, const char* label) {
  const int x = 28, w = 484, h = 100;
  M5Canvas c(&M5.Display);
  c.setColorDepth(lgfx::grayscale_8bit);
  c.setPsram(true);
  c.createSprite(w, h);
  int bg = done ? doneLevel : 15, ink = done ? 3 : 0;
  c.fillSprite(colorOf(15));
  c.fillSmoothRoundRect(0, 0, w, h, 22, colorOf(0));
  c.fillSmoothRoundRect(3, 3, w - 6, h - 6, 19, colorOf(bg));
  if (!done) c.fillSmoothRoundRect(18, 16, 68, 68, 16, colorOf(13));
  if (auto* b = icon("tooth", 48)) blitIcon(c, *b, 28, 26, ink, done ? bg : 13);
  c.setFont(&fonts::FreeSansBold18pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(colorOf(ink));
  c.drawString(label, 104, 50);
  int cx = w - 48, cy = 50;
  if (done) {
    c.fillSmoothCircle(cx, cy, 30, colorOf(0));
    if (auto* b = icon("ui_check", 40)) blitIcon(c, *b, cx - 20, cy - 20, 15, 0);
  } else {
    c.fillSmoothCircle(cx, cy, 30, colorOf(0));
    c.fillSmoothCircle(cx, cy, 27, colorOf(15));
  }
  c.pushSprite(x, y);
  c.deleteSprite();
}

void spPaletteContext() {
  Serial.println("SP-02b palette in context");
  deepClean();
  D.setEpdMode(epd_mode_t::epd_quality);
  title("SP-02b rows: todo, done L11, done L10");
  drawRowAt(50, false, 0, "Brush teeth");
  drawRowAt(162, true, 11, "Done L11");
  drawRowAt(274, true, 10, "Done L10");
  drawRowAt(386, false, 0, "Eat breakfast");

  // Keyboard strip: tray C_DONE, white keys, modifier keys C_DIVIDER. Old (11/9) vs new (10/8).
  D.setFont(&fonts::FreeSans9pt7b);
  D.setTextDatum(middle_left);
  D.setTextColor(TFT_BLACK, TFT_WHITE);
  for (int v = 0; v < 2; ++v) {
    int y = 510 + v * 120, tray = v ? 10 : 11, mod = v ? 8 : 9;
    D.drawString(v ? "keyboard: tray L10, shift L8" : "keyboard: tray L11, shift L9", 16, y - 14);
    D.fillRect(0, y, W, 96, colorOf(tray));
    D.fillRect(0, y, W, 4, colorOf(0));
    D.fillSmoothRoundRect(6, y + 16, 72, 64, 10, colorOf(0));
    D.fillSmoothRoundRect(8, y + 18, 68, 60, 8, colorOf(mod));
    for (int k = 0; k < 7; ++k) {
      int kx = 84 + k * 54;
      D.fillSmoothRoundRect(kx, y + 16, 48, 64, 10, colorOf(0));
      D.fillSmoothRoundRect(kx + 2, y + 18, 44, 60, 8, colorOf(15));
    }
    D.fillSmoothRoundRect(462, y + 16, 72, 64, 10, colorOf(0));
    D.fillSmoothRoundRect(464, y + 18, 68, 60, 8, colorOf(mod));
  }

  // Disabled vs enabled Add button: C_TILE fill, C_DIVIDER border (L8), C_MUTED text.
  D.setFont(&fonts::FreeSansBold12pt7b);
  D.setTextDatum(middle_center);
  D.fillSmoothRoundRect(24, 780, 492, 72, 18, colorOf(8));
  D.fillSmoothRoundRect(27, 783, 486, 66, 16, colorOf(13));
  D.setTextColor(colorOf(5), colorOf(13));
  D.drawString("Pick a picture (disabled)", W / 2, 816);
  D.fillSmoothRoundRect(24, 868, 492, 72, 18, colorOf(0));
  D.setTextColor(TFT_WHITE, TFT_BLACK);
  D.drawString("Add \"Feed the cat\" (enabled)", W / 2, 904);
  refresh(0, 0, W, H);
  Serial.println("SP-02b drawn");
}

// ---- SP-07: touch during refresh ----------------------------------------------------
void spTouchDuringRefresh() {
  Serial.println("SP-07 touch during refresh: tap once to start, then keep tapping for ~5 s");
  clearScreen();
  title("SP-07: tap to start, then keep tapping");
  refresh(0, 0, W, 60);
  waitTap(120000);
  uint32_t start = millis();
  int taps = 0, tapsBusy = 0;
  for (int i = 0; i < 5; ++i) {
    D.setEpdMode(epd_mode_t::epd_quality);
    D.fillScreen((i & 1) ? TFT_WHITE : colorOf(13));
    uint32_t t0 = millis();
    D.display(0, 0, W, H);
    bool wasBusy = true;
    while (wasBusy || millis() - t0 < 1000) {
      wasBusy = D.displayBusy();
      M5.update();
      auto t = M5.Touch.getDetail();
      if (t.wasPressed()) {
        ++taps;
        if (wasBusy) ++tapsBusy;
        Serial.printf("  t=%5lu ms tap at %d,%d  panel %s\n", millis() - start, t.x, t.y,
                      wasBusy ? "BUSY" : "idle");
      }
      delay(5);
    }
  }
  clearScreen();
  Serial.printf("SP-07 done: %d taps seen, %d while the panel was busy\n", taps, tapsBusy);
}

void printHelp() {
  Serial.println("commands: t=SP-01 timing  g=SP-02 gray ramp  r=SP-02b palette in context  h=SP-03 ghost DU  j=SP-03 ghost DU4");
  Serial.println("          f=SP-04 fonts  l=SP-04b label sizes  i=SP-05 icons  b=SP-09 blackout  k=SP-07 touch  c=clear  ?=help");
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  D.setRotation(0);
  D.setAutoDisplay(false);
  Serial.println();
  Serial.println("[spike] display spike ready");
  Serial.printf("[spike] PSRAM free %u KB\n", ESP.getFreePsram() / 1024);
  clearScreen();
  title("TaskPal display spike: waiting for serial commands");
  refresh(0, 0, W, 60);
  printHelp();
}

void loop() {
  M5.update();
  if (!Serial.available()) { delay(10); return; }
  char c = Serial.read();
  switch (c) {
    case 't': spTiming(5); break;
    case 'g': spGrayRamp(); break;
    case 'r': spPaletteContext(); break;
    case 'h': spGhosting(epd_mode_t::epd_fast, "DU"); break;
    case 'j': spGhosting(epd_mode_t::epd_fastest, "DU4"); break;
    case 'f': spFonts(); break;
    case 'l': spLabelSizes(); break;
    case 'i': spIcons(); break;
    case 'b': spBlackout(); break;
    case 'k': spTouchDuringRefresh(); break;
    case 'c': clearScreen(); break;
    case '?': printHelp(); break;
    default: break;
  }
}
