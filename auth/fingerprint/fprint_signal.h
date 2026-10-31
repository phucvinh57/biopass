#pragma once

#include <optional>
#include <string>

#include "auth_method.h"

namespace biopass {

// Pure decisions behind the fprintd VerifyStatus wait, kept free of D-Bus so
// they can be unit tested.

// Maps a VerifyStatus (result, done) pair to the result that ends the wait.
// std::nullopt means keep waiting for the next status.
std::optional<AuthResult> mapVerifyStatus(const std::string& status, bool done);

// True only when a VerifyStatus signal came from fprintd's resolved unique bus
// name and carries the "(sb)" payload. Anything else is ignored.
bool acceptVerifyStatus(const char* sender, const std::string& expected_owner, bool params_type_ok);

// True when a NameOwnerChanged for net.reactivated.Fprint means the fprintd
// instance we talk to is gone (the new owner is not the one we resolved).
bool fprintdOwnerLost(const std::string& new_owner, const std::string& expected_owner);

}  // namespace biopass
