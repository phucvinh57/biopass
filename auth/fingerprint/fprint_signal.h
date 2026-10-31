#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "auth_method.h"

namespace biopass {

// Pure decisions behind the fprintd VerifyStatus wait, kept free of D-Bus so
// they can be unit tested.

// Maps a VerifyStatus (result, done) pair to the result that ends the wait.
// std::nullopt means keep waiting for the next status.
std::optional<AuthResult> mapVerifyStatus(const std::string& status, bool done);

// Maps an EnrollStatus (result, done) pair to the end of the enroll wait:
// true when the finger is enrolled, false when enrollment failed, and
// std::nullopt to keep waiting for the next scan. Takes a string_view so the
// D-Bus signal handler doesn't allocate.
std::optional<bool> mapEnrollStatus(std::string_view status, bool done);

// True only when a VerifyStatus (or EnrollStatus) signal came from fprintd's
// resolved unique bus name and carries the "(sb)" payload. Anything else is
// ignored.
bool acceptVerifyStatus(const char* sender, const std::string& expected_owner, bool params_type_ok);

// True when a NameOwnerChanged for net.reactivated.Fprint means the fprintd
// instance we talk to is gone (the new owner is not the one we resolved).
bool fprintdOwnerLost(const std::string& new_owner, const std::string& expected_owner);

}  // namespace biopass
