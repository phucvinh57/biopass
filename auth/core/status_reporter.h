#pragma once

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <mutex>
#include <string>

namespace biopass {

// One-way status channel from biopass-helper to pam.cc, which relays each
// line through the PAM conversation (the helper's stdout/stderr must stay
// silent, see helper.cc). Format: "<KIND> <text>\n", KIND is INFO
// (PAM_TEXT_INFO) or ERROR (PAM_ERROR_MSG). No fd configured = no-op.
//
// Header-only: the Tauri app links libbiopass_fingerprint.a alone, so methods
// must not depend on biopass_core to report status.

namespace status_detail {

struct State {
  // Methods may run in parallel; serialize so lines never interleave.
  std::mutex mutex;
  int fd = -1;
};

inline State& state() {
  static State s;
  return s;
}

inline void writeLine(const char* kind, const std::string& message) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.fd < 0) {
    return;
  }

  // Flatten newlines so a message can't split or smuggle a second KIND.
  std::string line = std::string(kind) + " " + message;
  for (char& c : line) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  line.push_back('\n');

  const char* data = line.data();
  size_t remaining = line.size();
  while (remaining > 0) {
    ssize_t written = write(s.fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      // Reader is gone; stop reporting rather than affect authentication.
      close(s.fd);
      s.fd = -1;
      return;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }
}

}  // namespace status_detail

// Takes ownership of `fd`. A negative or invalid fd disables reporting.
inline void setStatusFd(int fd) {
  status_detail::State& s = status_detail::state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.fd >= 0) {
    close(s.fd);
  }
  s.fd = -1;
  if (fd < 0 || fcntl(fd, F_GETFD) < 0) {
    return;
  }
  // CLOEXEC: a lingering holder would delay pam.cc seeing EOF.
  // SIGPIPE ignored: a vanished reader must not kill the helper.
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  signal(SIGPIPE, SIG_IGN);
  s.fd = fd;
}

inline void reportInfo(const std::string& message) { status_detail::writeLine("INFO", message); }

inline void reportError(const std::string& message) { status_detail::writeLine("ERROR", message); }

}  // namespace biopass
