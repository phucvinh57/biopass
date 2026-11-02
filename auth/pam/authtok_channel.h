#pragma once

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <cstddef>

namespace biopass {

// One-shot channel carrying the saved password from biopass-helper to pam.cc
// after a successful biometric match (see docs/Keyring.md). The helper writes
// the raw bytes and closes the fd; pam.cc reads them once the helper has
// exited. Dependency-free: pam.cc (loaded into gdm, sudo, ...) includes it.

// Same limit as the sealed secret (keyring/secret_buffer.h).
inline constexpr size_t kMaxAuthtokLen = 128;

// 1..kMaxAuthtokLen bytes, no NUL (PAM_AUTHTOK is a C string).
inline bool isValidAuthtok(const char* data, size_t len) {
  return len > 0 && len <= kMaxAuthtokLen && memchr(data, 0, len) == nullptr;
}

// Writes all of `data`. False on error.
inline bool writeAuthtok(int fd, const char* data, size_t len) {
  while (len > 0) {
    const ssize_t n = write(fd, data, len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    data += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

// Reads what the helper left in the pipe into `buf` (at least
// kMaxAuthtokLen + 2 bytes) and NUL-terminates it. Call only after the helper
// has exited: the fd is switched to non-blocking, so a write end still held
// open somewhere ends the read instead of hanging it. Returns the length, or
// 0 when there is nothing valid (empty, too long, NUL inside, read error);
// on 0 the buffer is wiped.
inline size_t readAuthtok(int fd, char* buf, size_t buf_size) {
  if (buf_size < kMaxAuthtokLen + 2) {
    return 0;
  }
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return 0;
  }
  // One byte more than allowed, so an over-long value is seen and refused.
  const size_t want = kMaxAuthtokLen + 1;
  size_t got = 0;
  while (got < want) {
    const ssize_t n = read(fd, buf + got, want - got);
    if (n > 0) {
      got += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      got = 0;  // read error: drop whatever arrived
    }
    break;  // EOF, or nothing more to come
  }
  if (!isValidAuthtok(buf, got)) {
    explicit_bzero(buf, buf_size);
    return 0;
  }
  buf[got] = '\0';
  return got;
}

}  // namespace biopass
