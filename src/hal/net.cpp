#include "net.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <algorithm>
#include <ctime>

#include "clock.h"

namespace hal::net {

namespace {
constexpr const char* kNs = "net";
constexpr uint32_t kConnectMs = 15000;
constexpr uint32_t kNtpMs = 20000;

void wifiOff() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

const char* authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "Enterprise";
    default: return "other";
  }
}

// One SNTP request (RFC 4330, mode 3) to `host`; on success `out` = UTC seconds.
bool ntpQuery(const char* host, int64_t& out) {
  IPAddress ip;
  if (!ip.fromString(host) && (!WiFi.hostByName(host, ip) || ip == IPAddress(0, 0, 0, 0))) {
    ESP_LOGW("net", "NTP: DNS lookup of %s failed", host);
    return false;
  }
  WiFiUDP udp;
  if (!udp.begin(2390)) {
    ESP_LOGW("net", "NTP: UDP socket failed");
    return false;
  }
  uint8_t pkt[48] = {};
  pkt[0] = 0x1B;  // LI 0, version 3, client
  udp.beginPacket(ip, 123);
  udp.write(pkt, sizeof pkt);
  udp.endPacket();
  const uint32_t t0 = millis();
  bool ok = false;
  while (millis() - t0 < 3000) {
    if (udp.parsePacket() >= 48) {
      udp.read(pkt, sizeof pkt);
      const uint32_t secs = uint32_t(pkt[40]) << 24 | uint32_t(pkt[41]) << 16 | uint32_t(pkt[42]) << 8 | pkt[43];
      out = int64_t(secs) - 2208988800LL;  // NTP era 0 -> Unix
      ok = out > 1700000000LL;
      ESP_LOGI("net", "NTP: %s (%s) replied in %lu ms%s", host, ip.toString().c_str(), (unsigned long)(millis() - t0),
               ok ? "" : " with an invalid time");
      break;
    }
    delay(20);
  }
  if (!ok && millis() - t0 >= 3000) ESP_LOGW("net", "NTP: no reply from %s (%s)", host, ip.toString().c_str());
  udp.stop();
  return ok;
}

// Read-only Preferences fail loudly if the namespace was never written: create it instead.
std::string readPref(const char* key) {
  Preferences p;
  p.begin(kNs, false);
  const std::string v = p.isKey(key) ? p.getString(key, "").c_str() : "";
  p.end();
  return v;
}
}  // namespace

const char* signalWord(int rssi) { return rssi > -60 ? "strong" : (rssi >= -75 ? "ok" : "weak"); }

std::vector<Network> scan(size_t maxResults) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const int n = WiFi.scanNetworks();
  std::vector<Network> out;
  for (int i = 0; i < n; ++i) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    auto it = std::find_if(out.begin(), out.end(), [&](const Network& x) { return x.ssid == ssid; });
    if (it != out.end()) {
      it->rssi = std::max(it->rssi, int(WiFi.RSSI(i)));
      continue;
    }
    out.push_back({ssid, int(WiFi.RSSI(i)), WiFi.encryptionType(i) != WIFI_AUTH_OPEN, authName(WiFi.encryptionType(i))});
    ESP_LOGI("net", "scan: '%s' %d dBm ch %d %s", ssid.c_str(), int(WiFi.RSSI(i)), int(WiFi.channel(i)),
             authName(WiFi.encryptionType(i)));
  }
  WiFi.scanDelete();
  wifiOff();
  std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  if (out.size() > maxResults) out.resize(maxResults);
  return out;
}

bool hasCredentials() { return !savedSsid().empty(); }

std::string savedSsid() { return readPref("ssid"); }

void saveCredentials(const std::string& ssid, const std::string& password) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("ssid", ssid.c_str());
  p.putString("pass", password.c_str());
  p.end();
}

void forget() {
  Preferences p;
  p.begin(kNs, false);
  p.clear();
  p.end();
}

const char* describe(SyncResult r) {
  switch (r) {
    case SyncResult::Ok: return "Clock synced";
    case SyncResult::NoCredentials: return "No WiFi network saved";
    case SyncResult::ConnectFailed: return "Could not join the WiFi (check the password)";
    case SyncResult::NoTime: return "Joined WiFi, but no time from the internet";
  }
  return "";
}

SyncResult sync(const std::string& ssid, const std::string& password, const std::string& tz) {
  if (ssid.empty()) return SyncResult::NoCredentials;
  WiFi.mode(WIFI_STA);
  // Modem sleep made the link drop a few seconds after joining (BEACON_TIMEOUT on the user's mesh
  // router, M7): keep the radio awake for the short time WiFi is on.
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kConnectMs) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGW("net", "connect to '%s' failed (status %d, password length %u)", ssid.c_str(), int(WiFi.status()),
             unsigned(password.size()));
    wifiOff();
    return SyncResult::ConnectFailed;
  }
  ESP_LOGI("net", "connected to '%s' in %lu ms, IP %s, DNS %s", ssid.c_str(), (unsigned long)(millis() - t0),
           WiFi.localIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
  // Direct SNTP query instead of esp_sntp: in M7 esp_sntp never reported success on the user's
  // network and gave no reason. Each step here is logged.
  int64_t epoch = 0;
  delay(500);  // let DHCP/DNS settle
  // Names first; fixed addresses in case DNS is not working (Google, Cloudflare, NIST).
  // (A failed DNS lookup can take ~7 s, so only one name before the addresses.)
  const char* servers[] = {"pool.ntp.org", "216.239.35.0", "162.159.200.1", "129.6.15.28", "time.google.com"};
  bool ok = false;
  const uint32_t t1 = millis();
  for (const char* host : servers) {
    if (millis() - t1 > kNtpMs) break;
    if (ntpQuery(host, epoch)) {
      ok = true;
      break;
    }
  }
  if (ok) {
    clock::setEpoch(epoch);  // system time + RTC (UTC)
    ESP_LOGI("net", "NTP ok in %lu ms", (unsigned long)(millis() - t1));
  }
  wifiOff();
  clock::setTz(tz);
  return ok ? SyncResult::Ok : SyncResult::NoTime;
}

SyncResult syncSaved(const std::string& tz) { return sync(readPref("ssid"), readPref("pass"), tz); }

}  // namespace hal::net
