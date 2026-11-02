#pragma once

#include <string.h>
#include <sys/mman.h>

#include <cstddef>
#include <cstdint>

namespace biopass {

// Largest secret one TPM sealed object holds (MAX_SYM_DATA in the TPM 2.0
// spec). The PAM side has the same limit (pam/authtok_channel.h).
inline constexpr size_t kMaxSecretLen = 128;

// A password in a fixed buffer: locked in RAM (best effort) so it isn't
// swapped out, always NUL-terminated for crypt_r and pam_set_item, and wiped
// when cleared or destroyed. Never copied.
class SecretBuffer {
 public:
  SecretBuffer() { locked_ = mlock(data_, sizeof(data_)) == 0; }
  ~SecretBuffer() {
    clear();
    if (locked_) {
      munlock(data_, sizeof(data_));
    }
  }

  SecretBuffer(const SecretBuffer&) = delete;
  SecretBuffer& operator=(const SecretBuffer&) = delete;

  // False (and the buffer is left empty) if `len` exceeds kMaxSecretLen.
  bool assign(const void* src, size_t len) {
    clear();
    if (len > kMaxSecretLen) {
      return false;
    }
    memcpy(data_, src, len);
    size_ = len;
    return true;
  }

  // For writing in place (e.g. read()): fill data(), then setSize().
  uint8_t* data() { return data_; }
  const uint8_t* data() const { return data_; }
  const char* c_str() const { return reinterpret_cast<const char*>(data_); }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  bool setSize(size_t len) {
    if (len > kMaxSecretLen) {
      clear();
      return false;
    }
    size_ = len;
    data_[len] = 0;
    return true;
  }

  // True when the secret holds no NUL byte (it must survive as a C string).
  bool isCString() const { return memchr(data_, 0, size_) == nullptr; }

  bool equals(const SecretBuffer& other) const {
    if (size_ != other.size_) {
      return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < size_; ++i) {
      diff |= static_cast<uint8_t>(data_[i] ^ other.data_[i]);
    }
    return diff == 0;
  }

  void clear() {
    explicit_bzero(data_, sizeof(data_));
    size_ = 0;
  }

 private:
  // +1 keeps a terminating NUL after a full-length secret.
  uint8_t data_[kMaxSecretLen + 1] = {};
  size_t size_ = 0;
  bool locked_ = false;
};

}  // namespace biopass
