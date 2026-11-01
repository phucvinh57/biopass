#pragma once

// Exit codes of `biopass-helper auth`, the only channel from the helper to the
// PAM module (pam/helper_status.h maps them back to PAM codes). Keep the
// numbers stable: an older module may run a newer helper during upgrades.

namespace biopass {

// What AuthManager decided. Ignore = no method could even try (none
// configured, no camera, no fprintd), so PAM should fall through rather
// than count a failed attempt.
enum class AuthOutcome { Success, Failure, Ignore };

enum class HelperExit : int { Ok = 0, AuthErr = 1, Ignore = 2 };

constexpr HelperExit toHelperExit(AuthOutcome outcome) {
  switch (outcome) {
    case AuthOutcome::Success:
      return HelperExit::Ok;
    case AuthOutcome::Ignore:
      return HelperExit::Ignore;
    case AuthOutcome::Failure:
      break;
  }
  return HelperExit::AuthErr;
}

constexpr int exitCode(HelperExit e) { return static_cast<int>(e); }

}  // namespace biopass
