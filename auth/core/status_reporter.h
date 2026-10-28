#pragma once

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <mutex>
#include <string>

namespace biopass {

// One-way channel from biopass-helper back to the PAM module (pam.cc), which
// relays each line to the application through the PAM conversation function.
// The helper cannot talk to the conversation itself: it is a separate process
// and its stdout/stderr must stay silent (see setupBiopassLogger in helper.cc).
//
// Wire format: one message per line, "<KIND> <text>\n", where KIND is INFO
// (relayed as PAM_TEXT_INFO) or ERROR (relayed as PAM_ERROR_MSG).
//
// When no status fd is configured (helper run outside pam.cc, PAM_SILENT, or
// the "quiet" module option), every report is a no-op.
//
// Header-only on purpose: the Tauri app links libbiopass_fingerprint.a alone
// (app/src-tauri/build.rs), so methods must not gain a link-time dependency
// on biopass_core just to report status.

namespace status_detail {

struct State {
  // Methods may run on parallel threads (ExecutionMode::Parallel); serialize
  // writes so lines never interleave.
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

  // The protocol is line-based: flatten embedded newlines so one message can
  // never be split into two (or smuggle in a second KIND prefix).
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
  // Don't leak the channel into anything this process might exec (a lingering
  // holder would delay pam.cc noticing EOF), and don't let a vanished reader
  // kill the helper with SIGPIPE.
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  signal(SIGPIPE, SIG_IGN);
  s.fd = fd;
}

inline void reportInfo(const std::string& message) { status_detail::writeLine("INFO", message); }

inline void reportError(const std::string& message) { status_detail::writeLine("ERROR", message); }

}  // namespace biopass
