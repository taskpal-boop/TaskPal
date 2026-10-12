// settings.json / lists.json / progress.json <-> model (ARCHITECTURE §7).
// Decoding never fails hard: bad or missing input gives defaults, and every value is clamped
// to the model limits, so a corrupt file can't crash the UI.
#pragma once
#include <string>

#include "model.h"

namespace core {

std::string encodeSettings(const Settings& s);
std::string encodeLists(const ListSet& set);
std::string encodeProgress(const Progress& p);

// Return false if the text was not usable JSON; `out` then holds defaults.
// Files without "v" (schema 0) are read as schema 1.
bool decodeSettings(const std::string& json, Settings& out);
bool decodeLists(const std::string& json, ListSet& out);  // defaults = defaultLists()
bool decodeProgress(const std::string& json, Progress& out);

}  // namespace core
