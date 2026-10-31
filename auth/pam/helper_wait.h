#pragma once

#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <string>

#include "helper_status.h"

namespace biopass {

// Receives one status line from the helper, without its trailing newline.
using StatusLineFn = std::function<void(const std::string&)>;

// Waits for biopass-helper `pid` and returns the PAM result for it. While the
// helper runs, each line it writes to `status_fd` (-1 = none) is passed to
// `on_line`. Takes ownership of `status_fd` and closes it.
//
// Polls with a timeout instead of blocking until EOF so we still return if
// something else ends up holding the pipe's write end. Fails closed: if the
// helper can't be reaped (e.g. ECHILD because the caller ignores SIGCHLD or
// reaped it first), its exit status is unknown and the result is
// PAM_AUTH_ERR.
inline int waitForHelper(pid_t pid, int status_fd, const StatusLineFn& on_line) {
  std::string pending;
  bool fd_open = status_fd >= 0;
  int status = 0;
  bool reaped = false;

  for (;;) {
    if (fd_open) {
      struct pollfd pfd;
      pfd.fd = status_fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int ready = poll(&pfd, 1, 200);
      if (ready > 0) {
        char buf[512];
        ssize_t n = read(status_fd, buf, sizeof(buf));
        if (n > 0) {
          pending.append(buf, static_cast<size_t>(n));
          size_t nl;
          while ((nl = pending.find('\n')) != std::string::npos) {
            on_line(pending.substr(0, nl));
            pending.erase(0, nl + 1);
          }
          if (pending.size() > 4096) {
            pending.clear();  // Runaway line without a newline; drop it.
          }
          continue;
        }
        if (n == 0 || errno != EINTR) {
          fd_open = false;  // EOF or error: just wait for the child below.
        }
      } else if (ready < 0 && errno != EINTR) {
        fd_open = false;
      }
    }

    pid_t r = waitpid(pid, &status, fd_open ? WNOHANG : 0);
    if (r == pid) {
      reaped = true;
      break;
    }
    if (r < 0 && errno != EINTR) {
      break;
    }
  }

  if (status_fd >= 0) {
    close(status_fd);
  }
  if (!reaped) {
    return PAM_AUTH_ERR;  // We never saw the exit status, so it can't count.
  }
  return pamCodeForHelperStatus(status);
}

}  // namespace biopass
