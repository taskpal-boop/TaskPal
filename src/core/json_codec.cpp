#include "json_codec.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdlib>

#include "catalog.h"
#include "date.h"
#include "now_list.h"
#include "text_rules.h"

namespace core {

namespace {

std::string cleanLabel(const std::string& text, const char* fallback) {
  std::string t = trimmed(text);
  if (t.size() > kMaxLabelLen) t = trimmed(t.substr(0, kMaxLabelLen));
  return t.empty() ? fallback : t;
}

bool parse(const std::string& json, JsonDocument& doc) {
  return !deserializeJson(doc, json) && doc.is<JsonObject>();
}

}  // namespace

// ---- settings -------------------------------------------------------------------------
std::string encodeSettings(const Settings& s) {
  JsonDocument doc;
  doc["v"] = kSchemaVersion;
  doc["childName"] = s.childName;
  doc["resetHour"] = s.resetHour;
  doc["weeklyStars"] = s.weeklyStars;
  doc["sleepMin"] = s.sleepMin;
  doc["tz"] = s.tz;
  doc["weekStart"] = s.weekStartDow;
  if (!s.pinHash.empty()) {
    doc["pin"]["salt"] = s.pinSalt;
    doc["pin"]["hash"] = s.pinHash;
  }
  doc["lastSync"] = s.lastSync;
  doc["autoSync"] = s.autoSync;
  doc["autoRefresh"] = s.autoRefresh;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool decodeSettings(const std::string& json, Settings& out) {
  out = Settings{};
  JsonDocument doc;
  if (!parse(json, doc)) return false;
  out.childName = cleanLabel(doc["childName"] | "", "");
  out.resetHour = std::clamp(doc["resetHour"] | 4, 0, 23);
  out.weeklyStars = doc["weeklyStars"] | true;
  out.sleepMin = std::clamp(doc["sleepMin"] | 2, 1, 60);
  out.tz = doc["tz"] | Settings{}.tz.c_str();
  out.weekStartDow = std::clamp(doc["weekStart"] | 1, 0, 6);
  out.pinSalt = doc["pin"]["salt"] | "";
  out.pinHash = doc["pin"]["hash"] | "";
  if (out.pinSalt.empty() || out.pinHash.size() != 64) out.pinSalt.clear(), out.pinHash.clear();
  out.lastSync = doc["lastSync"] | int64_t(0);
  out.autoSync = doc["autoSync"] | true;
  out.autoRefresh = doc["autoRefresh"] | true;
  return true;
}

// ---- lists ----------------------------------------------------------------------------
std::string encodeLists(const ListSet& set) {
  JsonDocument doc;
  doc["v"] = kSchemaVersion;
  doc["nextId"] = set.nextId;
  JsonArray lists = doc["lists"].to<JsonArray>();
  for (const auto& l : set.lists) {
    JsonObject o = lists.add<JsonObject>();
    o["id"] = l.id;
    o["name"] = l.name;
    o["icon"] = l.icon;
    if (!l.anyTime()) {
      o["from"] = formatHhmm(l.fromMin);
      o["to"] = formatHhmm(l.toMin);
    }
    o["celebration"] = celebrationKey(l.celebration);
    JsonArray tasks = o["tasks"].to<JsonArray>();
    for (const auto& t : l.tasks) {
      JsonObject to = tasks.add<JsonObject>();
      to["id"] = t.id;
      to["label"] = t.label;
      to["icon"] = t.icon;
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool decodeLists(const std::string& json, ListSet& out) {
  JsonDocument doc;
  if (!parse(json, doc) || !doc["lists"].is<JsonArrayConst>()) {
    out = defaultLists();
    return false;
  }
  out = ListSet{};
  uint32_t maxId = 0;
  for (JsonObjectConst o : doc["lists"].as<JsonArrayConst>()) {
    if (out.lists.size() >= kMaxLists) break;
    TaskList l;
    l.id = o["id"] | 0u;
    l.name = cleanLabel(o["name"] | "", "List");
    const std::string icon = o["icon"] | "star";
    l.icon = isListIcon(icon) ? icon : "star";
    l.fromMin = parseHhmm(o["from"] | "");
    l.toMin = parseHhmm(o["to"] | "");
    if (l.fromMin < 0 || l.toMin < 0) l.fromMin = l.toMin = -1;
    l.celebration = celebrationFromKey(o["celebration"] | "star");  // missing (before D27) -> star
    for (JsonObjectConst t : o["tasks"].as<JsonArrayConst>()) {
      if (l.tasks.size() >= kMaxTasks) break;
      Task task;
      task.id = t["id"] | 0u;
      task.label = cleanLabel(t["label"] | "", "Task");
      const std::string ti = t["icon"] | "star";
      task.icon = isTaskIcon(ti) ? ti : "star";
      l.tasks.push_back(task);
    }
    out.lists.push_back(l);
  }
  // Repair ids: every list/task needs a unique non-zero id below nextId.
  for (auto& l : out.lists) {
    maxId = std::max(maxId, l.id);
    for (auto& t : l.tasks) maxId = std::max(maxId, t.id);
  }
  out.nextId = std::max<uint32_t>(doc["nextId"] | 1u, maxId + 1);
  std::set<uint32_t> seen;
  auto fix = [&](uint32_t& id) {
    if (id == 0 || !seen.insert(id).second) {
      id = out.nextId++;
      seen.insert(id);
    }
  };
  for (auto& l : out.lists) {
    fix(l.id);
    for (auto& t : l.tasks) fix(t.id);
  }
  return true;
}

// ---- progress -------------------------------------------------------------------------
namespace {

void encodeIdMap(JsonObject obj, const std::map<uint32_t, std::set<uint32_t>>& m) {
  for (const auto& [listId, ids] : m) {
    if (ids.empty()) continue;
    JsonArray a = obj[std::to_string(listId)].to<JsonArray>();
    for (uint32_t id : ids) a.add(id);
  }
}

void decodeIdMap(JsonObjectConst obj, std::map<uint32_t, std::set<uint32_t>>& m) {
  for (JsonPairConst kv : obj) {
    const uint32_t listId = uint32_t(std::strtoul(kv.key().c_str(), nullptr, 10));
    if (listId == 0) continue;
    for (JsonVariantConst v : kv.value().as<JsonArrayConst>()) {
      if (v.is<uint32_t>()) m[listId].insert(v.as<uint32_t>());
    }
  }
}

}  // namespace

std::string encodeProgress(const Progress& p) {
  JsonDocument doc;
  doc["v"] = kSchemaVersion;
  doc["day"] = p.day;
  encodeIdMap(doc["done"].to<JsonObject>(), p.done);
  JsonObject stars = doc["stars"].to<JsonObject>();
  for (const auto& [day, n] : p.stars) stars[day] = n;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool decodeProgress(const std::string& json, Progress& out) {
  out = Progress{};
  JsonDocument doc;
  if (!parse(json, doc)) return false;
  Date d;
  const std::string day = doc["day"] | "";
  if (parseIso(day, d)) out.day = day;
  decodeIdMap(doc["done"].as<JsonObjectConst>(), out.done);  // a "credit" key from before D25 is ignored
  for (JsonPairConst kv : doc["stars"].as<JsonObjectConst>()) {
    Date sd;
    const int n = kv.value() | 0;
    if (parseIso(kv.key().c_str(), sd) && n > 0) out.stars[kv.key().c_str()] = n;
  }
  return true;
}

}  // namespace core
