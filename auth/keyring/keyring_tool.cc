// biopass-keyring: saves or forgets the user's login password for keyring
// unlock (docs/Keyring.md). Run only as `pkexec biopass-keyring <cmd>` from
// the Biopass app; the polkit action (com.ticklab.biopass.policy) asks the
// user to authenticate as themselves.
//
//   seal [--pcr7]  password on stdin: verify against /etc/shadow, TPM-seal,
//                  store /var/lib/biopass/keyring/<uid>.sealed, and
//                  <uid>.mode saying whether it is bound to PCR 7
//   forget         delete both files
//
// The user is always the one who ran pkexec (PKEXEC_UID), never an argument.
// Exit codes: keyring_exit.h.

#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "keyring_exit.h"
#include "pkexec_uid.h"
#include "sealed_file.h"
#include "sealed_store.h"
#include "secret_buffer.h"
#include "shadow_check.h"
#include "tpm_seal.h"

namespace {

using biopass::KeyringExit;

constexpr auto kStdinTimeout = std::chrono::seconds(30);
// Slows password guessing: nothing like pam_faillock guards this path.
constexpr auto kWrongPasswordDelay = std::chrono::seconds(2);

int fail(KeyringExit code, const char* message) {
  std::fprintf(stderr, "biopass-keyring: %s\n", message);
  return biopass::keyringExitCode(code);
}

std::optional<std::string> usernameFor(uid_t uid) {
  std::vector<char> buf(16384);
  struct passwd pw{};
  struct passwd* result = nullptr;
  if (getpwuid_r(uid, &pw, buf.data(), buf.size(), &result) != 0 || result == nullptr ||
      pw.pw_name == nullptr || pw.pw_name[0] == '\0') {
    return std::nullopt;
  }
  return std::string(pw.pw_name);
}

// Reads stdin to EOF into `out`. False when it is empty, longer than
// kMaxSecretLen, holds a NUL, or doesn't end within kStdinTimeout.
bool readPassword(biopass::SecretBuffer& out) {
  const auto deadline = std::chrono::steady_clock::now() + kStdinTimeout;
  uint8_t chunk[biopass::kMaxSecretLen + 1];
  size_t got = 0;
  bool ok = false;
  for (;;) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      break;
    }
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&pfd, 1, static_cast<int>(left.count()));
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready <= 0) {
      break;
    }
    const ssize_t n = read(STDIN_FILENO, chunk + got, sizeof(chunk) - got);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      break;
    }
    if (n == 0) {
      ok = got > 0 && got <= biopass::kMaxSecretLen;
      break;
    }
    got += static_cast<size_t>(n);
    if (got > biopass::kMaxSecretLen) {
      break;  // too long
    }
  }
  ok = ok && out.assign(chunk, got) && out.isCString();
  explicit_bzero(chunk, sizeof(chunk));
  if (!ok) {
    out.clear();
  }
  return ok;
}

int seal(uid_t uid, const std::string& username, bool pcr7) {
  biopass::SecretBuffer password;
  if (!readPassword(password)) {
    return fail(KeyringExit::BadInput, "password must be 1-128 bytes on stdin");
  }

  switch (biopass::verifyLocalPassword(username, password.c_str())) {
    case biopass::PasswordCheck::Match:
      break;
    case biopass::PasswordCheck::Mismatch:
      std::this_thread::sleep_for(kWrongPasswordDelay);
      return fail(KeyringExit::WrongPassword, "wrong password");
    case biopass::PasswordCheck::NoLocalAccount:
    case biopass::PasswordCheck::Locked:
      return fail(KeyringExit::NoLocalAccount, "no local password for this account");
    case biopass::PasswordCheck::Error:
      return fail(KeyringExit::NoLocalAccount, "unsupported password hash");
  }

  std::unique_ptr<biopass::TpmContext> tpm = biopass::TpmContext::openDevice();
  if (!tpm) {
    return fail(KeyringExit::NoTpm, "no TPM (/dev/tpmrm0)");
  }
  biopass::SealedBlob blob;
  switch (tpm->seal(password, pcr7, uid, blob)) {
    case biopass::TpmStatus::Ok:
      break;
    case biopass::TpmStatus::NoTpm:
      return fail(KeyringExit::NoTpm, "no TPM");
    case biopass::TpmStatus::OwnerAuthSet:
      return fail(KeyringExit::OwnerAuthSet, "TPM owner hierarchy has a password");
    case biopass::TpmStatus::PcrUnsupported:
      return fail(KeyringExit::PcrUnsupported, "TPM has no SHA-256 PCR bank");
    default:
      return fail(KeyringExit::TpmError, "TPM sealing failed");
  }
  password.clear();

  const std::vector<uint8_t> data = biopass::serializeSealed(blob);
  if (data.empty()) {
    return fail(KeyringExit::TpmError, "could not encode the sealed password");
  }
  const biopass::SealedStore store;
  // Drop the old mode first: if anything below fails, the app shows the mode
  // as unknown rather than wrong.
  biopass::removeSealedMode(store, uid);
  if (biopass::writeSealed(store, uid, data) != biopass::StoreResult::Ok) {
    return fail(KeyringExit::StorageError, "could not write /var/lib/biopass/keyring");
  }
  // Only for display; the password is already saved, so this can't fail the save.
  biopass::writeSealedMode(store, uid, pcr7);
  return biopass::keyringExitCode(KeyringExit::Ok);
}

int forget(uid_t uid) {
  const biopass::SealedStore store;
  biopass::removeSealedMode(store, uid);
  switch (biopass::removeSealed(store, uid)) {
    case biopass::StoreResult::Ok:
    case biopass::StoreResult::NotFound:
      return biopass::keyringExitCode(KeyringExit::Ok);
    default:
      return fail(KeyringExit::StorageError, "could not delete the saved password");
  }
}

}  // namespace

int main(int argc, char** argv) {
  // No core dumps or ptrace of a process holding the password.
  prctl(PR_SET_DUMPABLE, 0);
  const struct rlimit no_core = {0, 0};
  setrlimit(RLIMIT_CORE, &no_core);
  umask(077);
  // tpm2-tss logs to stderr by default; the app shows only our own line.
  setenv("TSS2_LOG", "all+NONE", 1);

  if (argc < 2) {
    return fail(KeyringExit::Usage, "usage: biopass-keyring seal [--pcr7] | forget");
  }
  const std::string cmd = argv[1];
  bool pcr7 = false;
  if (cmd == "seal" && argc == 3 && strcmp(argv[2], "--pcr7") == 0) {
    pcr7 = true;
  } else if (!((cmd == "seal" || cmd == "forget") && argc == 2)) {
    return fail(KeyringExit::Usage, "usage: biopass-keyring seal [--pcr7] | forget");
  }

  // Root through pkexec only: pkexec sets PKEXEC_UID itself, and only a
  // process that already is root can fake it.
  if (getuid() != 0 || geteuid() != 0) {
    return fail(KeyringExit::NotAuthorized, "run through pkexec");
  }
  const std::optional<uid_t> uid = biopass::parsePkexecUid(getenv("PKEXEC_UID"));
  if (!uid) {
    return fail(KeyringExit::NotAuthorized, "run through pkexec by a regular user");
  }

  if (cmd == "forget") {
    return forget(*uid);
  }
  const std::optional<std::string> username = usernameFor(*uid);
  if (!username) {
    return fail(KeyringExit::NoLocalAccount, "unknown user");
  }
  return seal(*uid, *username, pcr7);
}
