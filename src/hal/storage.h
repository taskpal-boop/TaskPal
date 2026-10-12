// LittleFS for user data only (settings/lists/progress JSON); fonts live in flash (D15).
// Saves are atomic: write "<path>.tmp", then rename over "<path>" (ARCHITECTURE §7).
#pragma once
#include <cstddef>
#include <string>

namespace hal::storage {

bool begin();  // mounts the "spiffs" partition; formats it only if it can't be mounted
bool read(const char* path, std::string& out);
bool writeAtomic(const char* path, const std::string& data);
bool exists(const char* path);
bool remove(const char* path);
int removeDir(const char* dir);  // deletes the files in dir and the dir itself; returns files removed
int removeTempFiles();  // leftovers from a save interrupted by a power cut; returns count
// Appends one line to a small log file, keeping it under maxBytes (oldest half dropped). M8 soak.
bool appendLine(const char* path, const std::string& line, size_t maxBytes = 32768);
size_t usedBytes();
size_t totalBytes();

}  // namespace hal::storage
