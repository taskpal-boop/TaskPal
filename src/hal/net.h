// WiFi + NTP time sync (ARCHITECTURE §8, M7). WiFi is on only while scanning or syncing.
// Credentials live in NVS (Preferences "net"), never in the JSON files.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hal::net {

struct Network {
  std::string ssid;
  int rssi;        // dBm; above -60 strong, -60..-75 ok, below -75 weak
  bool secure;
  std::string auth; // "open", "WPA2", "WPA3", "WPA2/3", ...
};

// "strong" / "ok" / "weak" from RSSI.
const char* signalWord(int rssi);

// Blocking scan (about 2-4 s): strongest first, duplicates and hidden networks removed.
std::vector<Network> scan(size_t maxResults = 8);

bool hasCredentials();
std::string savedSsid();
void saveCredentials(const std::string& ssid, const std::string& password);
void forget();

enum class SyncResult { Ok, NoCredentials, ConnectFailed, NoTime };
const char* describe(SyncResult r);

// Connects, gets the time over NTP, writes system time + RTC, then turns WiFi off. Blocking:
// up to about 15 s to connect + 10 s for NTP. `tz` = POSIX TZ string.
SyncResult sync(const std::string& ssid, const std::string& password, const std::string& tz);
SyncResult syncSaved(const std::string& tz);

}  // namespace hal::net
