#include "storage.h"

#include <LittleFS.h>

#include <vector>

namespace hal::storage {

bool begin() {
  if (LittleFS.begin(false, "/littlefs", 10, "spiffs")) return true;
  ESP_LOGW("storage", "LittleFS mount failed: formatting");
  return LittleFS.begin(true, "/littlefs", 10, "spiffs");
}

bool read(const char* path, std::string& out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  out.resize(f.size());
  const size_t n = out.empty() ? 0 : f.read(reinterpret_cast<uint8_t*>(&out[0]), out.size());
  f.close();
  return n == out.size();
}

bool writeAtomic(const char* path, const std::string& data) {
  const std::string tmp = std::string(path) + ".tmp";
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f) return false;
  const size_t n = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.flush();
  f.close();
  if (n != data.size()) {
    LittleFS.remove(tmp.c_str());
    return false;
  }
  // littlefs rename replaces an existing target atomically: after a power cut the file is
  // either the old or the new version, never half-written (PWR-06).
  if (LittleFS.rename(tmp.c_str(), path)) return true;
  // Fallback for a VFS that refuses to replace: not atomic, so only used if rename fails.
  LittleFS.remove(path);
  return LittleFS.rename(tmp.c_str(), path);
}

bool exists(const char* path) { return LittleFS.exists(path); }
bool remove(const char* path) { return LittleFS.remove(path); }

int removeDir(const char* dir) {
  if (!LittleFS.exists(dir)) return 0;
  std::vector<String> files;
  File d = LittleFS.open(dir);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) files.push_back(f.path());
  d.close();
  for (const auto& p : files) LittleFS.remove(p);
  LittleFS.rmdir(dir);
  return int(files.size());
}

bool appendLine(const char* path, const std::string& line, size_t maxBytes) {
  File f = LittleFS.open(path, "r");
  const size_t size = f ? f.size() : 0;
  if (f) f.close();
  if (size + line.size() + 1 > maxBytes) {
    // Keep the newest half, starting at a line boundary.
    std::string text;
    if (read(path, text)) {
      size_t cut = text.find('\n', text.size() / 2);
      text = cut == std::string::npos ? std::string() : text.substr(cut + 1);
      writeAtomic(path, text);
    }
  }
  File a = LittleFS.open(path, "a");
  if (!a) return false;
  a.print(line.c_str());
  a.print('\n');
  a.close();
  return true;
}

int removeTempFiles() {
  std::vector<String> tmp;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = f.path();
    if (name.endsWith(".tmp")) tmp.push_back(name);
  }
  for (const auto& p : tmp) LittleFS.remove(p);
  return int(tmp.size());
}

size_t usedBytes() { return LittleFS.usedBytes(); }
size_t totalBytes() { return LittleFS.totalBytes(); }

}  // namespace hal::storage
