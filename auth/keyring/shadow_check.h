#pragma once

#include <string>

namespace biopass {

enum class PasswordCheck {
  Match,
  Mismatch,
  NoLocalAccount,  // no /etc/shadow entry (LDAP, SSSD, ...)
  Unreadable,      // /etc/shadow can't be read: not root, or denied by SELinux
  Locked,          // empty, "!..." or "*..." hash: no password login
  Error,           // crypt_r failed (unsupported hash method, ...)
};

// Compares `password` with the crypt(3) hash `hash` in constant time.
// Locked or empty hashes never match.
PasswordCheck verifyAgainstHash(const char* password, const char* hash);

// Looks up `username` in /etc/shadow (needs root) and verifies `password`.
PasswordCheck verifyLocalPassword(const std::string& username, const char* password);

}  // namespace biopass
