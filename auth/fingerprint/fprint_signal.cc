#include "fprint_signal.h"

namespace biopass {

std::optional<AuthResult> mapVerifyStatus(const std::string& status, bool done) {
  if (status == "verify-match") {
    return AuthResult::Success;
  }
  if (status == "verify-unknown-error" || status == "verify-disconnected") {
    return AuthResult::Unavailable;
  }
  if (!done) {
    // Non-final no-match, retry scan, swipe too short, etc.
    return std::nullopt;
  }
  if (status == "verify-no-match") {
    return AuthResult::Failure;
  }
  return AuthResult::Retry;
}

bool acceptVerifyStatus(const char* sender, const std::string& expected_owner,
                        bool params_type_ok) {
  if (!params_type_ok || sender == nullptr || expected_owner.empty()) {
    return false;
  }
  return expected_owner == sender;
}

bool fprintdOwnerLost(const std::string& new_owner, const std::string& expected_owner) {
  return expected_owner.empty() || new_owner != expected_owner;
}

}  // namespace biopass
