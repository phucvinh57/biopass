#pragma once

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

#include "helper_status.h"

namespace biopass {

// Upper bound on one biopass-helper run. Past it the helper is killed and PAM
// moves on to the next module (usually the password prompt), so a stuck
// camera or fprintd can't lock anyone out. The per-method limits that
// readConfig() clamps keep a normal run well below this.
inline constexpr std::chrono::seconds kHelperTimeout{60};

// How long a SIGKILLed helper gets to be reaped. A process stuck in an
// uninterruptible syscall (e.g. a wedged V4L2 driver) can't die until the
// syscall returns, and the login must not wait for that.
inline constexpr std::chrono::milliseconds kHelperKillGrace{1000};

// Receives one status line from the helper, without its trailing newline.
using StatusLineFn = std::function<void(const std::string&)>;

namespace detail {

using Clock = std::chrono::steady_clock;

// Milliseconds left until `deadline`, at most `cap`; <= 0 once it passed.
inline int msUntil(Clock::time_point deadline, int cap) {
  auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
  return static_cast<int>(std::min<std::chrono::milliseconds::rep>(left.count(), cap));
}

// Relays status lines and polls for the helper's exit until `deadline`.
// Returns true once the helper's fate is known (`*result` set): it was reaped,
// or waitpid() failed (ECHILD: the caller ignores SIGCHLD or reaped it first;
// we never saw the status, so PAM_AUTH_ERR). Returns false when the deadline
// passed with the helper still running. May throw (allocation, `on_line`).
inline bool relayUntilExit(pid_t pid, int status_fd, Clock::time_point deadline,
                           const StatusLineFn& on_line, int* result) {
  std::string pending;
  bool fd_open = status_fd >= 0;

  for (;;) {
    int left = msUntil(deadline, 200);
    if (left <= 0) {
      return false;
    }

    if (fd_open) {
      struct pollfd pfd;
      pfd.fd = status_fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int ready = poll(&pfd, 1, left);
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
    } else {
      poll(nullptr, 0, std::min(left, 20));  // No fd to wait on: short sleep.
    }

    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      *result = pamCodeForHelperStatus(status);
      return true;
    }
    if (r < 0 && errno != EINTR) {
      *result = PAM_AUTH_ERR;
      return true;
    }
  }
}

// A pidfd pins the helper process itself, so signalling through it can never
// hit an unrelated process that reused the pid (possible when the caller
// ignores SIGCHLD and the kernel reaps the helper behind our back).
struct HelperRef {
  pid_t pid;
  int pidfd = -1;  // -1 when pidfd_open() failed
  int open_errno = 0;

  explicit HelperRef(pid_t p) noexcept : pid(p) {
#ifdef SYS_pidfd_open
    pidfd = static_cast<int>(syscall(SYS_pidfd_open, p, 0));  // O_CLOEXEC is implied
    open_errno = pidfd < 0 ? errno : 0;
#else
    open_errno = ENOSYS;
#endif
  }
  ~HelperRef() {
    if (pidfd >= 0) {
      close(pidfd);
    }
  }
  HelperRef(const HelperRef&) = delete;
  HelperRef& operator=(const HelperRef&) = delete;

  void sigkill() const noexcept {
#ifdef SYS_pidfd_send_signal
    if (pidfd >= 0) {
      if (syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0) == 0 || errno != ENOSYS) {
        return;  // Sent, or the helper is already gone (ESRCH).
      }
    }
#endif
    if (open_errno == ESRCH) {
      return;  // Gone before we could pin it; its pid may belong to someone else now.
    }
    kill(pid, SIGKILL);  // Kernel without pidfds.
  }
};

// SIGKILLs the helper and reaps it if that happens within kHelperKillGrace.
// The helper doesn't create its own process group, so only it is killed.
inline void killAndReap(const HelperRef& helper) noexcept {
  const pid_t pid = helper.pid;
  helper.sigkill();
  const auto deadline = Clock::now() + kHelperKillGrace;
  for (;;) {
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid || (r < 0 && errno != EINTR)) {
      return;
    }
    int left = msUntil(deadline, 10);
    if (left <= 0) {
      return;  // Unkillable for now; leave it rather than hang the login.
    }
    poll(nullptr, 0, left);
  }
}

}  // namespace detail

// Waits for biopass-helper `pid` for at most `timeout` and returns the PAM
// result for it. While the helper runs, each line it writes to `status_fd`
// (-1 = none) is passed to `on_line`. Takes ownership of `status_fd`.
//
// Fails closed: only a reaped helper that exited with HelperExit::Ok is
// PAM_SUCCESS. On timeout, or if relaying throws, the helper is killed and
// the result is PAM_AUTH_ERR: the helper may have been mid-scan, so this
// counts as a failed attempt rather than "no method could run" (PAM_IGNORE).
// Uses a monotonic clock, so wall-clock changes don't move the deadline.
// Call it right after fork(): that's when the helper gets pinned by a pidfd.
inline int waitForHelper(pid_t pid, int status_fd, std::chrono::milliseconds timeout,
                         const StatusLineFn& on_line) noexcept {
  const detail::HelperRef helper(pid);
  const auto deadline = detail::Clock::now() + timeout;
  int result = PAM_AUTH_ERR;
  bool settled = false;
  try {
    settled = detail::relayUntilExit(pid, status_fd, deadline, on_line, &result);
  } catch (...) {
    settled = false;
  }
  if (status_fd >= 0) {
    close(status_fd);
  }
  if (!settled) {
    detail::killAndReap(helper);
    return PAM_AUTH_ERR;
  }
  return result;
}

}  // namespace biopass
