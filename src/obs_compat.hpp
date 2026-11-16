#pragma once
#include <string>

// One representative patch is tested per minor series. Prereleases and newer
// minor series require a separate compatibility decision.
inline bool SupportsObsRuntime(const std::string& version) {
 for (const char* prefix : {"32.0.", "32.1.", "32.2.", "33.0."}) {
  const std::string series(prefix);
  if (version.rfind(series, 0) != 0) continue;
  const auto patch = version.substr(series.size());
  if (patch.empty() || (patch.size() > 1 && patch[0] == '0')) return false;
  return patch.find_first_not_of("0123456789") == std::string::npos;
 }
 return false;
}
