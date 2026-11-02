#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace biopass {

// Where sealed passwords live. `dir` is created 0711 (others may stat their
// own file by name, not list it) and its parent 0755, both owned by `owner`;
// files are <uid>.sealed, 0600. Every component the store opens must be owned
// by `owner` and not group/other writable, and is opened without following
// symlinks. Tests point it at a temp dir owned by the test user.
struct SealedStore {
  std::string dir = "/var/lib/biopass/keyring";
  uid_t owner = 0;
};

enum class StoreResult { Ok, NotFound, Unsafe, Error };

std::string sealedFileName(uid_t uid);

// Atomically replaces the file for `uid` (temp file, fsync, rename, fsync
// dir), creating the directories as needed.
StoreResult writeSealed(const SealedStore& store, uid_t uid, const std::vector<uint8_t>& data);

// Reads the file for `uid` (at most kMaxSealedFileSize bytes).
StoreResult readSealed(const SealedStore& store, uid_t uid, std::vector<uint8_t>& out);

// Deletes the file for `uid`. NotFound when there was none.
StoreResult removeSealed(const SealedStore& store, uid_t uid);

}  // namespace biopass
