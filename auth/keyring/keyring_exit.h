#pragma once

namespace biopass {

// Exit codes of biopass-keyring, read by the app (app/src-tauri/src/keyring.rs
// keeps the same numbers). 126 and 127 are pkexec's own (not authorized,
// dismissed); stay clear of them.
enum class KeyringExit : int {
  Ok = 0,
  Usage = 2,
  NotAuthorized = 3,  // not run as root through pkexec, or bad PKEXEC_UID
  WrongPassword = 4,
  NoLocalAccount = 5,  // no usable /etc/shadow entry (LDAP, SSSD, locked)
  BadInput = 6,        // empty, longer than 128 bytes, NUL inside, or timed out
  NoTpm = 7,
  OwnerAuthSet = 8,
  PcrUnsupported = 9,
  TpmError = 10,
  StorageError = 11,
};

constexpr int keyringExitCode(KeyringExit e) { return static_cast<int>(e); }

}  // namespace biopass
