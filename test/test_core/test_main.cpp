// Unit tests for src/core (UT-01..UT-20).
// Host:   pio test -e native
#include <unity.h>

#include <string>

#include "core/battery.h"
#include "core/catalog.h"
#include "core/date.h"
#include "core/json_codec.h"
#include "core/logic_day.h"
#include "core/model.h"
#include "core/now_list.h"
#include "core/pin.h"
#include "core/stars.h"
#include "core/suggest.h"
#include "core/text_rules.h"

using namespace core;

void setUp() {}
void tearDown() {}

namespace {
Date D(int y, int m, int d) { return Date{y, m, d}; }
LocalTime T(int y, int m, int d, int h, int mi) { return LocalTime{D(y, m, d), h, mi}; }
void assertDate(const Date& expected, const Date& actual) {
  TEST_ASSERT_EQUAL_STRING(toIso(expected).c_str(), toIso(actual).c_str());
}
}  // namespace

// ---- dates (support) ------------------------------------------------------------------
void test_date_roundtrip_and_weekday() {
  assertDate(D(1970, 1, 1), civilFromDays(0));
  TEST_ASSERT_EQUAL(4, dayOfWeek(D(1970, 1, 1)));  // Thursday
  TEST_ASSERT_EQUAL(6, dayOfWeek(D(2026, 9, 26)));  // Saturday
  TEST_ASSERT_EQUAL(3, dayOfWeek(D(1969, 12, 31)));  // Wednesday (before the epoch)
  assertDate(D(2024, 3, 1), addDays(D(2024, 2, 28), 2));  // leap year
  Date parsed;
  TEST_ASSERT_TRUE(parseIso("2026-09-27", parsed));
  assertDate(D(2026, 9, 27), parsed);
  TEST_ASSERT_FALSE(parseIso("2026-02-30", parsed));
  TEST_ASSERT_FALSE(parseIso("26-9-27", parsed));
}

// ---- UT-01 / UT-02: logical day ----------------------------------------------------
void test_UT01_logical_day_reset_boundary() {
  assertDate(D(2026, 9, 26), logicalDay(T(2026, 9, 27, 3, 59), 4));
  assertDate(D(2026, 9, 27), logicalDay(T(2026, 9, 27, 4, 0), 4));
  assertDate(D(2026, 9, 30), logicalDay(T(2026, 10, 1, 2, 0), 4));  // across a month
  assertDate(D(2025, 12, 31), logicalDay(T(2026, 1, 1, 0, 30), 4));  // across a year
}

void test_UT02_reset_hour_edges() {
  assertDate(D(2026, 9, 27), logicalDay(T(2026, 9, 27, 0, 0), 0));  // midnight reset
  assertDate(D(2026, 9, 26), logicalDay(T(2026, 9, 27, 22, 59), 23));
  assertDate(D(2026, 9, 27), logicalDay(T(2026, 9, 27, 23, 0), 23));
}

// ---- UT-03 / UT-04 / UT-06 / UT-07: rollover and stars ------------------------------
void test_UT03_day_rollover_clears_progress() {
  ListSet lists = defaultLists();
  Progress p;
  rollover(p, D(2026, 9, 26), 1);
  check(p, 1, 2);
  TEST_ASSERT_FALSE(rollover(p, D(2026, 9, 26), 1));  // same day: nothing changes
  TEST_ASSERT_TRUE(isDone(p, 1, 2));
  TEST_ASSERT_TRUE(rollover(p, D(2026, 9, 27), 1));
  TEST_ASSERT_FALSE(isDone(p, 1, 2));
  TEST_ASSERT_TRUE(p.done.empty());
  TEST_ASSERT_EQUAL(1, p.stars["2026-09-26"]);  // yesterday's star is kept
  TEST_ASSERT_EQUAL(3, (int)lists.lists.size());  // lists untouched
  TEST_ASSERT_EQUAL_STRING("2026-09-27", p.day.c_str());
}

void test_UT04_week_rollover() {
  assertDate(D(2026, 9, 21), weekStart(D(2026, 9, 27), 1));  // Sunday -> Monday before
  assertDate(D(2026, 9, 28), weekStart(D(2026, 9, 28), 1));  // Monday is its own start
  assertDate(D(2026, 9, 27), weekStart(D(2026, 9, 27), 0));  // Sunday-start weeks
  Progress p;
  p.stars = {{"2026-09-20", 4}, {"2026-09-21", 3}, {"2026-09-27", 2}};
  p.day = "2026-09-27";
  TEST_ASSERT_EQUAL(5, weeklyStars(p, D(2026, 9, 27), 1));  // Sunday: Mon 21 .. Sun 27
  rollover(p, D(2026, 9, 28), 1);                           // Monday: new week
  TEST_ASSERT_EQUAL(0, weeklyStars(p, D(2026, 9, 28), 1));
  TEST_ASSERT_EQUAL(0, (int)p.stars.count("2026-09-20"));    // older weeks pruned
}

void test_UT05_dst_days() {
  // Spring forward (EU 2026-03-29: 02:00 -> 03:00). Local 03:00 exists, 02:30 doesn't.
  assertDate(D(2026, 3, 29), logicalDay(T(2026, 3, 29, 3, 0), 2));
  assertDate(D(2026, 3, 28), logicalDay(T(2026, 3, 29, 1, 59), 2));
  // Fall back (EU 2026-10-25: 03:00 -> 02:00, so 02:xx happens twice). Both are the same day.
  assertDate(D(2026, 10, 25), logicalDay(T(2026, 10, 25, 2, 30), 2));
  // US spring forward 2026-03-08 with the default 04:00 reset: unaffected.
  assertDate(D(2026, 3, 7), logicalDay(T(2026, 3, 8, 3, 30), 4));
  assertDate(D(2026, 3, 8), logicalDay(T(2026, 3, 8, 4, 0), 4));
}

void test_UT06_star_credit() {
  Progress p;
  rollover(p, D(2026, 9, 23), 1);  // a Wednesday, so the next day is in the same week
  check(p, 1, 2);
  check(p, 1, 2);  // double tap: still one star
  TEST_ASSERT_EQUAL(1, starsToday(p));
  uncheck(p, 1, 2);  // same day: star given back
  TEST_ASSERT_EQUAL(0, starsToday(p));
  check(p, 1, 2);
  check(p, 1, 3);
  check(p, 2, 7);
  TEST_ASSERT_EQUAL(3, starsToday(p));
  uncheck(p, 1, 3);  // list completed by accident: unchecking still takes the star back (D25)
  TEST_ASSERT_EQUAL(2, starsToday(p));
  check(p, 1, 3);
  restartList(p, 1);  // "Start this list again": list 1's checks and their stars go
  TEST_ASSERT_FALSE(isDone(p, 1, 2));
  TEST_ASSERT_EQUAL(1, starsToday(p));  // list 2's star stays
  check(p, 1, 2);  // re-checking earns it again
  TEST_ASSERT_EQUAL(2, starsToday(p));
  uncheck(p, 1, 9);  // unknown task: no change
  restartList(p, 5);  // unknown list: no change
  TEST_ASSERT_EQUAL(2, starsToday(p));
  // Next day: the checks are cleared and yesterday's stars can't be taken back.
  rollover(p, D(2026, 9, 24), 1);
  uncheck(p, 1, 2);
  restartList(p, 2);
  TEST_ASSERT_EQUAL(0, starsToday(p));
  TEST_ASSERT_EQUAL(2, weeklyStars(p, D(2026, 9, 24), 1));
  check(p, 1, 2);  // a new check earns a new star, which can be taken back
  TEST_ASSERT_EQUAL(3, weeklyStars(p, D(2026, 9, 24), 1));
  uncheck(p, 1, 2);
  TEST_ASSERT_EQUAL(0, starsToday(p));
  TEST_ASSERT_EQUAL(2, weeklyStars(p, D(2026, 9, 24), 1));
}

void test_UT07_weekly_total() {
  Progress p;
  p.stars = {{"2026-09-21", 5}, {"2026-09-23", 4}, {"2026-09-26", 6}, {"2026-09-28", 9}};
  TEST_ASSERT_EQUAL(15, weeklyStars(p, D(2026, 9, 26), 1));  // Mon 21..Sat 26 (28 is "future")
  TEST_ASSERT_EQUAL(9, weeklyStars(p, D(2026, 9, 28), 1));
}

// ---- UT-08 / UT-09: JSON ----------------------------------------------------------------
void test_UT08_json_roundtrip() {
  Settings s;
  s.childName = "Sam";
  s.resetHour = 5;
  s.weeklyStars = false;
  s.autoSync = false;
  s.autoRefresh = false;
  s.tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  s.pinSalt = "00112233";
  s.pinHash = hashPin("1234", s.pinSalt);
  Settings s2;
  TEST_ASSERT_TRUE(decodeSettings(encodeSettings(s), s2));
  TEST_ASSERT_EQUAL_STRING("Sam", s2.childName.c_str());
  TEST_ASSERT_EQUAL(5, s2.resetHour);
  TEST_ASSERT_FALSE(s2.weeklyStars);
  TEST_ASSERT_FALSE(s2.autoSync);
  TEST_ASSERT_FALSE(s2.autoRefresh);
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", s2.tz.c_str());
  Settings fresh;
  TEST_ASSERT_TRUE(decodeSettings("{}", fresh));
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", fresh.tz.c_str());  // D23 default
  TEST_ASSERT_TRUE(fresh.autoSync);
  TEST_ASSERT_TRUE(fresh.autoRefresh);  // D30 default on, also for settings saved before it
  TEST_ASSERT_EQUAL_STRING(s.pinHash.c_str(), s2.pinHash.c_str());

  ListSet lists = defaultLists();
  lists.lists[2].fromMin = lists.lists[2].toMin = -1;  // "Any time"
  ListSet lists2;
  TEST_ASSERT_TRUE(decodeLists(encodeLists(lists), lists2));
  TEST_ASSERT_EQUAL_STRING(encodeLists(lists).c_str(), encodeLists(lists2).c_str());
  TEST_ASSERT_TRUE(lists2.lists[2].anyTime());
  TEST_ASSERT_EQUAL(390, lists2.lists[0].fromMin);
  // D27: each list keeps its own celebration; missing or unknown -> star.
  TEST_ASSERT_EQUAL(int(Celebration::Star), int(lists2.lists[0].celebration));
  lists.lists[0].celebration = Celebration::Corgi;
  lists.lists[1].celebration = Celebration::Poop;
  TEST_ASSERT_TRUE(decodeLists(encodeLists(lists), lists2));
  TEST_ASSERT_EQUAL(int(Celebration::Corgi), int(lists2.lists[0].celebration));
  TEST_ASSERT_EQUAL(int(Celebration::Poop), int(lists2.lists[1].celebration));
  TEST_ASSERT_EQUAL(int(Celebration::Star), int(lists2.lists[2].celebration));
  TEST_ASSERT_TRUE(decodeLists(R"({"lists":[{"id":1,"name":"A","celebration":"unicorn","tasks":[]},)"
                               R"({"id":2,"name":"B","tasks":[]}]})", lists2));
  TEST_ASSERT_EQUAL(int(Celebration::Star), int(lists2.lists[0].celebration));
  TEST_ASSERT_EQUAL(int(Celebration::Star), int(lists2.lists[1].celebration));

  Progress p;
  rollover(p, D(2026, 9, 27), 1);
  check(p, 1, 2);
  check(p, 7, 9);
  p.stars["2026-09-22"] = 4;
  Progress p2;
  TEST_ASSERT_TRUE(decodeProgress(encodeProgress(p), p2));
  TEST_ASSERT_EQUAL_STRING(encodeProgress(p).c_str(), encodeProgress(p2).c_str());
}

void test_UT09_bad_json_gives_defaults() {
  Settings s;
  TEST_ASSERT_FALSE(decodeSettings("{not json", s));
  TEST_ASSERT_EQUAL(4, s.resetHour);
  TEST_ASSERT_FALSE(decodeSettings("", s));
  ListSet l;
  TEST_ASSERT_FALSE(decodeLists("[]", l));
  TEST_ASSERT_EQUAL(3, (int)l.lists.size());  // factory lists
  Progress p;
  TEST_ASSERT_FALSE(decodeProgress("garbage", p));
  TEST_ASSERT_TRUE(p.day.empty());

  // Schema 0 (no "v"), out-of-range values, unknown icons, too many tasks, duplicate ids.
  TEST_ASSERT_TRUE(decodeSettings(R"({"resetHour":30,"weekStart":9,"childName":"  Alexandrina-Josephine  "})", s));
  TEST_ASSERT_EQUAL(23, s.resetHour);
  TEST_ASSERT_EQUAL(6, s.weekStartDow);
  TEST_ASSERT_EQUAL(18, (int)s.childName.size());
  const char* lists = R"({"lists":[{"id":1,"name":"A","icon":"rocket","tasks":[
      {"id":1,"label":"x","icon":"tooth"},{"id":2,"label":"b","icon":"nope"},{"id":3,"label":"c","icon":"bed"},
      {"id":4,"label":"d","icon":"bed"},{"id":5,"label":"e","icon":"bed"},{"id":6,"label":"f","icon":"bed"},
      {"id":7,"label":"g","icon":"bed"},{"id":8,"label":"h","icon":"bed"},{"id":9,"label":"i","icon":"bed"}]}]})";
  TEST_ASSERT_TRUE(decodeLists(lists, l));
  TEST_ASSERT_EQUAL_STRING("star", l.lists[0].icon.c_str());
  TEST_ASSERT_EQUAL(8, (int)l.lists[0].tasks.size());
  TEST_ASSERT_EQUAL_STRING("star", l.lists[0].tasks[1].icon.c_str());
  TEST_ASSERT_NOT_EQUAL(l.lists[0].id, l.lists[0].tasks[0].id);  // duplicate id 1 repaired
  TEST_ASSERT_TRUE(l.nextId > l.lists[0].tasks[0].id);
  TEST_ASSERT_TRUE(decodeProgress(R"({"day":"bad","stars":{"2026-09-21":3,"x":2,"2026-09-22":-1}})", p));
  TEST_ASSERT_TRUE(p.day.empty());
  TEST_ASSERT_EQUAL(1, (int)p.stars.size());
}

// ---- UT-10 / UT-11 / UT-12: list and task editing -------------------------------------
void test_UT10_limits() {
  ListSet set = defaultLists();
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL(Result::Ok, addList(set, "New list", "star"));
  TEST_ASSERT_EQUAL(Result::Full, addList(set, "Seventh", "star"));
  const uint32_t morning = set.lists[0].id;  // 6 tasks
  TEST_ASSERT_EQUAL(Result::Ok, addTask(set, morning, "Feed the cat", "star"));
  TEST_ASSERT_EQUAL(Result::Ok, addTask(set, morning, "Make bed", "bed"));
  TEST_ASSERT_EQUAL(Result::Full, addTask(set, morning, "Ninth", "bed"));
  const uint32_t after = set.lists[1].id;
  TEST_ASSERT_EQUAL(Result::Invalid, addTask(set, after, "Nineteen characters", "bed"));  // 19
  TEST_ASSERT_EQUAL(Result::Ok, addTask(set, after, "Practice the piano", "music"));    // 18
  TEST_ASSERT_EQUAL(Result::Invalid, addTask(set, after, "   ", "bed"));
  TEST_ASSERT_EQUAL(Result::Invalid, addTask(set, after, "Swim", "rocket"));
}

void test_UT11_delete_cleans_progress() {
  ListSet set = defaultLists();
  Progress p;
  rollover(p, D(2026, 9, 27), 1);
  const uint32_t morning = set.lists[0].id, teeth = set.lists[0].tasks[1].id;
  check(p, morning, teeth);
  TEST_ASSERT_EQUAL(Result::Ok, removeTask(set, p, morning, teeth));
  TEST_ASSERT_FALSE(isDone(p, morning, teeth));
  TEST_ASSERT_EQUAL(5, (int)set.lists[0].tasks.size());
  TEST_ASSERT_EQUAL(1, starsToday(p));  // stars already earned stay
  const uint32_t before = set.nextId;
  uint32_t id = 0;
  addTask(set, morning, "Brush teeth", "tooth", &id);
  TEST_ASSERT_EQUAL(before, id);  // never reuses the deleted id
  TEST_ASSERT_NOT_EQUAL(teeth, id);
  check(p, set.lists[1].id, set.lists[1].tasks[0].id);
  TEST_ASSERT_EQUAL(Result::Ok, removeList(set, p, set.lists[1].id));
  TEST_ASSERT_EQUAL(2, (int)set.lists.size());
  TEST_ASSERT_EQUAL(1, (int)p.done.size());
  TEST_ASSERT_EQUAL(Result::NotFound, removeList(set, p, 999));
}

void test_UT12_reorder() {
  ListSet set = defaultLists();
  const uint32_t first = set.lists[0].id, last = set.lists[2].id;
  TEST_ASSERT_EQUAL(Result::Invalid, moveList(set, first, -1));
  TEST_ASSERT_EQUAL(Result::Invalid, moveList(set, last, +1));
  TEST_ASSERT_EQUAL(Result::Ok, moveList(set, first, +1));
  TEST_ASSERT_EQUAL(first, set.lists[1].id);
  auto& tasks = set.lists[1].tasks;  // Morning, now second
  const uint32_t t0 = tasks[0].id, t1 = tasks[1].id;
  TEST_ASSERT_EQUAL(Result::Invalid, moveTask(set, first, t0, -1));
  TEST_ASSERT_EQUAL(Result::Ok, moveTask(set, first, t0, +1));
  TEST_ASSERT_EQUAL(t1, tasks[0].id);
  TEST_ASSERT_EQUAL(t0, tasks[1].id);
  TEST_ASSERT_EQUAL(Result::Invalid, moveTask(set, first, tasks.back().id, +1));
}

// ---- UT-13 .. UT-16: typing, auto-label, Add button, suggestions ----------------------
void type(TextField& f, const char* s) {
  for (; *s; ++s) typeChar(f, *s);
}

void test_UT13_text_rules() {
  TextField f;
  TEST_ASSERT_EQUAL(KeyResult::Ignored, typeSpace(f));  // no leading space
  type(f, "feed");
  TEST_ASSERT_EQUAL_STRING("Feed", f.text.c_str());  // first letter auto-capitalised
  typeSpace(f);
  TEST_ASSERT_EQUAL(KeyResult::Ignored, typeSpace(f));  // no double space
  f.shift = true;
  type(f, "tHe");
  TEST_ASSERT_EQUAL_STRING("Feed THE", f.text.c_str());  // Shift is a toggle (D22): stays on
  TEST_ASSERT_TRUE(f.shift);
  f.shift = false;
  type(f, " cat and dog");
  TEST_ASSERT_EQUAL(18, (int)f.text.size());
  TEST_ASSERT_EQUAL_STRING("Feed THE cat and d", f.text.c_str());
  TEST_ASSERT_EQUAL(KeyResult::AtLimit, typeChar(f, 'x'));
  TEST_ASSERT_TRUE(backspace(f));
  TEST_ASSERT_EQUAL(17, (int)f.text.size());
  TEST_ASSERT_EQUAL_STRING("Tidy room", trimmed("  Tidy room ").c_str());
  // Password field (M7): no auto-capital, spaces anywhere, longer limit.
  TextField pw;
  pw.autoCap = false;
  pw.freeSpaces = true;
  pw.maxLen = 63;
  TEST_ASSERT_EQUAL(KeyResult::Added, typeSpace(pw));
  type(pw, "ab");
  typeSpace(pw);
  typeSpace(pw);
  TEST_ASSERT_EQUAL_STRING(" ab  ", pw.text.c_str());
  TEST_ASSERT_EQUAL_STRING("", trimmed("   ").c_str());
}

void test_UT14_auto_label() {
  NewTaskDraft d;
  pickIcon(d, "tooth");
  TEST_ASSERT_EQUAL_STRING("Brush teeth", d.text.c_str());  // picture first fills a name
  pickIcon(d, "bowl");
  TEST_ASSERT_EQUAL_STRING("Eat breakfast", d.text.c_str());  // still auto: replaced
  setTextByUser(d, "Feed the cat");
  pickIcon(d, "star");
  TEST_ASSERT_EQUAL_STRING("Feed the cat", d.text.c_str());  // typed text is kept
  TEST_ASSERT_EQUAL_STRING("star", d.icon.c_str());
  setTextByUser(d, "");  // clearing turns auto back on
  pickIcon(d, "bed");
  TEST_ASSERT_EQUAL_STRING("Make bed", d.text.c_str());
}

void test_UT15_add_button() {
  NewTaskDraft d;
  AddButton b = addButton(d);
  TEST_ASSERT_FALSE(b.enabled);
  TEST_ASSERT_EQUAL_STRING("Pick a picture", b.label.c_str());
  setTextByUser(d, "Feed the cat");
  TEST_ASSERT_EQUAL_STRING("Pick a picture", addButton(d).label.c_str());
  d = NewTaskDraft{};
  pickIcon(d, "star");
  setTextByUser(d, "   ");
  b = addButton(d);
  TEST_ASSERT_FALSE(b.enabled);
  TEST_ASSERT_EQUAL_STRING("Type a task name", b.label.c_str());
  setTextByUser(d, "Feed the cat ");
  b = addButton(d);
  TEST_ASSERT_TRUE(b.enabled);
  TEST_ASSERT_EQUAL_STRING("Add \u201CFeed the cat\u201D", b.label.c_str());
}

void test_UT16_suggestions() {
  auto s = suggestions("fe");
  TEST_ASSERT_EQUAL(2, (int)s.size());
  TEST_ASSERT_EQUAL_STRING("Feed the cat", s[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Feed the dog", s[1].c_str());
  s = suggestions("P");  // Put on shoes, Pack lunch, Pajamas on, Practice piano, Put toys away
  TEST_ASSERT_EQUAL(3, (int)s.size());
  s = suggestions("Homework");  // exact match excluded
  TEST_ASSERT_EQUAL(0, (int)s.size());
  s = suggestions("");
  TEST_ASSERT_EQUAL(3, (int)s.size());
  TEST_ASSERT_EQUAL_STRING("Feed the dog", s[0].c_str());
  TEST_ASSERT_EQUAL_STRING("Tidy room", s[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Wash hands", s[2].c_str());
  TEST_ASSERT_EQUAL(0, (int)suggestions("zz").size());
}

// ---- UT-17: PIN ----------------------------------------------------------------------------
void test_UT17_pin() {
  // SHA-256 test vectors (FIPS 180-2).
  auto h = sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                           toHex(h.data(), h.size()).c_str());
  const std::string long56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  h = sha256(reinterpret_cast<const uint8_t*>(long56.data()), long56.size());
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                           toHex(h.data(), h.size()).c_str());

  TEST_ASSERT_TRUE(isValidPin("0420"));
  TEST_ASSERT_FALSE(isValidPin("12a4"));
  TEST_ASSERT_FALSE(isValidPin("12345"));
  const std::string salt = "a1b2c3d4e5f60718", hash = hashPin("2468", salt);
  TEST_ASSERT_NOT_EQUAL(0, hash.compare(hashPin("2468", "ffffffffffffffff")));  // salt matters
  PinGuard g;
  TEST_ASSERT_EQUAL(PinResult::Ok, verifyPin(g, "2468", salt, hash, 1000));
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(PinResult::Wrong, verifyPin(g, "0000", salt, hash, 1000));
  int64_t retry = 0;
  TEST_ASSERT_EQUAL(PinResult::Locked, verifyPin(g, "0000", salt, hash, 1000, &retry));  // 5th
  TEST_ASSERT_EQUAL(60, (int)retry);
  TEST_ASSERT_EQUAL(PinResult::Locked, verifyPin(g, "2468", salt, hash, 1059));  // even correct
  TEST_ASSERT_EQUAL(PinResult::Ok, verifyPin(g, "2468", salt, hash, 1060));
}

// ---- UT-18 / UT-19 / UT-20 -------------------------------------------------------------
void test_UT18_now_list() {
  ListSet set = defaultLists();
  TEST_ASSERT_EQUAL(0, nowListIndex(set, 7 * 60));        // Morning 06:30-08:00
  TEST_ASSERT_EQUAL(-1, nowListIndex(set, 8 * 60));       // end is exclusive
  TEST_ASSERT_EQUAL(1, nowListIndex(set, 16 * 60));       // After school
  TEST_ASSERT_EQUAL(2, nowListIndex(set, 19 * 60 + 30));  // Bedtime
  TEST_ASSERT_EQUAL(-1, nowListIndex(set, 12 * 60));
  set.lists[0].fromMin = set.lists[0].toMin = -1;  // Morning -> "Any time"
  TEST_ASSERT_EQUAL(-1, nowListIndex(set, 7 * 60));
  set.lists[1].fromMin = 19 * 60;  // overlap with Bedtime: first in order wins
  set.lists[1].toMin = 21 * 60;
  TEST_ASSERT_EQUAL(1, nowListIndex(set, 19 * 60 + 30));
  TEST_ASSERT_TRUE(inWindow(22 * 60, 2 * 60, 1 * 60));  // across midnight
  TEST_ASSERT_FALSE(inWindow(22 * 60, 2 * 60, 12 * 60));
  TEST_ASSERT_EQUAL(390, parseHhmm("06:30"));
  TEST_ASSERT_EQUAL(-1, parseHhmm("24:00"));
  TEST_ASSERT_EQUAL_STRING("20:30", formatHhmm(1230).c_str());
  // D30: next screen change. Defaults: Morning 06:30-08:00, After school 15:00-18:00, Bedtime 19:00-20:30.
  ListSet d = defaultLists();
  TEST_ASSERT_EQUAL(30, minutesToNextChange(d, 6 * 60));        // 06:00 -> Morning starts 06:30
  TEST_ASSERT_EQUAL(90, minutesToNextChange(d, 6 * 60 + 30));   // at a boundary -> the next one (08:00)
  TEST_ASSERT_EQUAL(240, minutesToNextChange(d, 8 * 60));       // 08:00 -> greeting at 12:00
  TEST_ASSERT_EQUAL(180, minutesToNextChange(d, 12 * 60));      // 12:00 -> After school 15:00
  TEST_ASSERT_EQUAL(60, minutesToNextChange(d, 18 * 60));       // 18:00 (end + greeting) -> 19:00
  TEST_ASSERT_EQUAL(450, minutesToNextChange(d, 20 * 60 + 30)); // 20:30 -> greeting 04:00 (across midnight)
  ListSet none;
  TEST_ASSERT_EQUAL(480, minutesToNextChange(none, 4 * 60));    // no lists: greeting 04:00 -> 12:00
  TEST_ASSERT_EQUAL(1, minutesToNextChange(none, 18 * 60 - 1)); // always at least 1 minute ahead
}

void test_UT19_greeting() {
  TEST_ASSERT_EQUAL_STRING("Good morning!", greeting(4, "").c_str());
  TEST_ASSERT_EQUAL_STRING("Good morning, Sam!", greeting(11, " Sam ").c_str());
  TEST_ASSERT_EQUAL_STRING("Good afternoon!", greeting(12, "").c_str());
  TEST_ASSERT_EQUAL_STRING("Good evening!", greeting(18, "").c_str());
  TEST_ASSERT_EQUAL_STRING("Good evening!", greeting(3, "").c_str());
}

void test_UT20_reset_hour_stepper() {
  TEST_ASSERT_EQUAL(0, stepHour(23, +1));
  TEST_ASSERT_EQUAL(23, stepHour(0, -1));
  TEST_ASSERT_EQUAL(5, stepHour(4, +1));
}

void test_UT21_battery_percent() {
  TEST_ASSERT_EQUAL(100, batteryPercent(4300));
  TEST_ASSERT_EQUAL(100, batteryPercent(4150));
  TEST_ASSERT_EQUAL(50, batteryPercent(3800));
  TEST_ASSERT_EQUAL(0, batteryPercent(kBatteryEmptyMv));  // D28 switch-off point
  TEST_ASSERT_EQUAL(0, batteryPercent(3000));
  // Points from the 4.5-day run: 3640 mV had ~8 h left (M5Unified said 42%), 3604 mV ~5 h.
  TEST_ASSERT_TRUE(batteryPercent(3640) <= 15);
  TEST_ASSERT_TRUE(batteryPercent(3604) <= 10);
  int last = 101;  // never increases as the voltage drops
  for (int mv = 4200; mv >= 3400; mv -= 10) {
    TEST_ASSERT_TRUE(batteryPercent(mv) <= last);
    last = batteryPercent(mv);
  }
}

void test_catalog() {
  TEST_ASSERT_EQUAL_STRING("Brush teeth", suggestedLabel("tooth").c_str());
  TEST_ASSERT_EQUAL_STRING("", suggestedLabel("rocket").c_str());
  TEST_ASSERT_TRUE(isListIcon("moon"));
  TEST_ASSERT_FALSE(isTaskIcon("moon"));
  // D26: Home & chores pictures are real task icons with valid suggested labels.
  TEST_ASSERT_EQUAL_STRING("Wash hands", suggestedLabel("hands").c_str());
  TEST_ASSERT_EQUAL(19, taskIconIndex("hands"));
  int perGroup[kIconGroupCount] = {};
  for (const auto& i : kTaskIcons) {
    ++perGroup[int(i.group)];
    TEST_ASSERT_TRUE(isValidLabel(i.label));
    TEST_ASSERT_NOT_NULL(i.tile);
  }
  TEST_ASSERT_EQUAL(16, perGroup[int(IconGroup::Everyday)]);
  TEST_ASSERT_EQUAL(8, perGroup[int(IconGroup::Home)]);
  ListSet lists = defaultLists();
  TEST_ASSERT_EQUAL(int(Result::Ok), int(addTask(lists, lists.lists[0].id, "Wash hands", "hands")));
  ListSet back;
  TEST_ASSERT_TRUE(decodeLists(encodeLists(lists), back));
  TEST_ASSERT_EQUAL_STRING("hands", back.lists[0].tasks.back().icon.c_str());  // not replaced by "star"
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_date_roundtrip_and_weekday);
  RUN_TEST(test_catalog);
  RUN_TEST(test_UT01_logical_day_reset_boundary);
  RUN_TEST(test_UT02_reset_hour_edges);
  RUN_TEST(test_UT03_day_rollover_clears_progress);
  RUN_TEST(test_UT04_week_rollover);
  RUN_TEST(test_UT05_dst_days);
  RUN_TEST(test_UT06_star_credit);
  RUN_TEST(test_UT07_weekly_total);
  RUN_TEST(test_UT08_json_roundtrip);
  RUN_TEST(test_UT09_bad_json_gives_defaults);
  RUN_TEST(test_UT10_limits);
  RUN_TEST(test_UT11_delete_cleans_progress);
  RUN_TEST(test_UT12_reorder);
  RUN_TEST(test_UT13_text_rules);
  RUN_TEST(test_UT14_auto_label);
  RUN_TEST(test_UT15_add_button);
  RUN_TEST(test_UT16_suggestions);
  RUN_TEST(test_UT17_pin);
  RUN_TEST(test_UT18_now_list);
  RUN_TEST(test_UT19_greeting);
  RUN_TEST(test_UT20_reset_hour_stepper);
  RUN_TEST(test_UT21_battery_percent);
  return UNITY_END();
}
