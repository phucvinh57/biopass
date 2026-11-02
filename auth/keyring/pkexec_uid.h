#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>

namespace biopass {

// Parses PKEXEC_UID, which pkexec sets to the uid of the user who ran it.
// Strict: decimal digits only (no sign, space or leading zero), no overflow,
// never root (0) or (uid_t)-1. Anything else is nullopt.
inline std::optional<uid_t> parsePkexecUid(const char* text) {
  if (text == nullptr || text[0] == '\0' || text[0] == '0') {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return std::nullopt;
    }
    value = value * 10 + static_cast<uint64_t>(*p - '0');
    if (value >= static_cast<uint64_t>(static_cast<uid_t>(-1))) {
      return std::nullopt;
    }
  }
  return static_cast<uid_t>(value);
}

}  // namespace biopass
