#include "shadow_check.h"

#include <crypt.h>
#include <errno.h>
#include <shadow.h>
#include <string.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace biopass {

namespace {

bool isLockedHash(const char* hash) {
  return hash == nullptr || hash[0] == '\0' || hash[0] == '!' || hash[0] == '*';
}

bool constantTimeEquals(const char* a, const char* b) {
  const size_t len_a = strlen(a);
  const size_t len_b = strlen(b);
  if (len_a != len_b) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t i = 0; i < len_a; ++i) {
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
}

struct CryptDataDeleter {
  void operator()(crypt_data* data) const {
    explicit_bzero(data, sizeof(*data));
    delete data;
  }
};

}  // namespace

PasswordCheck verifyAgainstHash(const char* password, const char* hash) {
  if (isLockedHash(hash)) {
    return PasswordCheck::Locked;
  }
  // ~32 KiB: on the heap, zeroed before first use as crypt_r requires.
  std::unique_ptr<crypt_data, CryptDataDeleter> data(new crypt_data());
  const char* computed = crypt_r(password, hash, data.get());
  // libxcrypt returns a string starting with '*' on failure, never null,
  // but other implementations may.
  if (computed == nullptr || computed[0] == '*') {
    return PasswordCheck::Error;
  }
  return constantTimeEquals(computed, hash) ? PasswordCheck::Match : PasswordCheck::Mismatch;
}

PasswordCheck verifyLocalPassword(const std::string& username, const char* password) {
  std::vector<char> buf(4096);
  struct spwd entry{};
  struct spwd* result = nullptr;
  for (;;) {
    const int rc = getspnam_r(username.c_str(), &entry, buf.data(), buf.size(), &result);
    if (rc == ERANGE && buf.size() < (1u << 20)) {
      buf.assign(buf.size() * 2, '\0');
      continue;
    }
    if (rc != 0 || result == nullptr) {
      explicit_bzero(buf.data(), buf.size());
      return PasswordCheck::NoLocalAccount;
    }
    break;
  }
  const PasswordCheck check = verifyAgainstHash(password, entry.sp_pwdp);
  explicit_bzero(buf.data(), buf.size());
  return check;
}

}  // namespace biopass
