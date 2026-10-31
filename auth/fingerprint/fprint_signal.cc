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

std::optional<bool> mapEnrollStatus(std::string_view status, bool done) {
  if (status == "enroll-completed") {
    return true;
  }
  if (status == "enroll-unknown-error" || status == "enroll-disconnected") {
    return false;
  }
  if (!done) {
    // Stage passed, retry scan, swipe too short, etc.
    return std::nullopt;
  }
  // Only enroll-completed means the finger was stored; any other final
  // status (failed, data-full, duplicate, ...) is a failure.
  return false;
}

bool enrollWaitExpired(std::chrono::steady_clock::duration since_start,
                       std::chrono::steady_clock::duration since_status) {
  return since_status >= kEnrollIdleTimeout || since_start >= kEnrollMaxDuration;
}

bool isNoEnrolledPrints(std::string_view remote_error) {
  return remote_error == "net.reactivated.Fprint.Error.NoEnrolledPrints";
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
