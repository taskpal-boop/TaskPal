// Parent PIN: 4 digits stored as SHA-256(salt || pin), with a lockout after 5 wrong tries (D9).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

constexpr int kPinLength = 4;
constexpr int kMaxPinFailures = 5;
constexpr int64_t kPinLockSeconds = 60;

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len);
std::string toHex(const uint8_t* data, size_t len);

bool isValidPin(const std::string& pin);  // exactly 4 digits

// saltHex comes from the platform RNG (e.g. 16 random bytes -> toHex).
std::string hashPin(const std::string& pin, const std::string& saltHex);

// Kept in RTC memory on the device so a lockout survives sleep.
struct PinGuard {
  int failures = 0;
  int64_t lockedUntil = 0;  // epoch seconds
};

enum class PinResult { Ok, Wrong, Locked };

PinResult verifyPin(PinGuard& guard, const std::string& pin, const std::string& saltHex,
                    const std::string& hashHex, int64_t nowSec, int64_t* retryInSec = nullptr);

}  // namespace core
