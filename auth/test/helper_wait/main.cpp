// Runs the PAM module's helper wait loop (pam/helper_wait.h) against fake
// helpers: forked children that exit, write status lines or hang. Exits
// non-zero on any failure.

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "helper_wait.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

void writeAll(int fd, const char* text) {
  size_t len = std::strlen(text);
  while (len > 0) {
    ssize_t n = write(fd, text, len);
    if (n <= 0) {
      return;
    }
    text += n;
    len -= static_cast<size_t>(n);
  }
}

struct Fake {
  pid_t pid;
  int fd;  // read end of the status pipe, -1 without one
};

// Forks a fake helper running `body(write_fd)`; it exits with whatever
// `body` returns. `write_fd` is -1 without a pipe.
template <typename Body>
Fake spawn(bool with_pipe, Body body) {
  int p[2] = {-1, -1};
  if (with_pipe && pipe(p) != 0) {
    std::perror("pipe");
    _exit(2);
  }
  pid_t pid = fork();
  if (pid == 0) {
    if (p[0] >= 0) {
      close(p[0]);
    }
    _exit(body(p[1]));
  }
  if (p[1] >= 0) {
    close(p[1]);
  }
  return Fake{pid, p[0]};
}

using std::chrono::milliseconds;

int waitFake(const Fake& f, std::vector<std::string>* lines = nullptr,
             milliseconds timeout = milliseconds(5000)) {
  return biopass::waitForHelper(f.pid, f.fd, timeout, [lines](const std::string& line) {
    if (lines != nullptr) {
      lines->push_back(line);
    }
  });
}

// Body of a helper that never exits.
int hang(int) {
  for (;;) pause();
}

// Runs a wait with a short deadline; checks it returned PAM_AUTH_ERR in time
// and left no helper behind.
void expectKilled(const Fake& f, const char* what, std::vector<std::string>* lines = nullptr) {
  const milliseconds deadline(300);
  const auto start = std::chrono::steady_clock::now();
  int rc = waitFake(f, lines, deadline);
  const auto took =
      std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start);
  std::string label(what);
  expect(rc == PAM_AUTH_ERR, (label + " -> PAM_AUTH_ERR").c_str());
  expect(took >= deadline && took < deadline + milliseconds(700),
         (label + ": returns within deadline + slack").c_str());
  // Reaped by waitForHelper: the pid is no longer our child.
  expect(waitpid(f.pid, nullptr, WNOHANG) < 0 && errno == ECHILD,
         (label + ": helper killed and reaped").c_str());
}

int exitWith(int code) {
  return waitFake(spawn(true, [code](int) { return code; }));
}

}  // namespace

int main() {
  expect(exitWith(0) == PAM_SUCCESS, "exit 0 -> PAM_SUCCESS");
  expect(exitWith(1) == PAM_AUTH_ERR, "exit 1 -> PAM_AUTH_ERR");
  expect(exitWith(2) == PAM_IGNORE, "exit 2 -> PAM_IGNORE");
  expect(exitWith(7) == PAM_AUTH_ERR, "unknown exit -> PAM_AUTH_ERR");

  expect(waitFake(spawn(true,
                        [](int) {
                          raise(SIGKILL);
                          return 0;
                        })) == PAM_AUTH_ERR,
         "killed by a signal -> PAM_AUTH_ERR");

  expect(waitFake(spawn(false, [](int) { return 0; })) == PAM_SUCCESS,
         "no status pipe: exit 0 -> PAM_SUCCESS");

  {
    std::vector<std::string> lines;
    int rc = waitFake(spawn(true,
                            [](int fd) {
                              writeAll(fd, "INFO looking\nERROR cov");
                              usleep(50 * 1000);
                              writeAll(fd, "ered\nno newline");
                              return 0;
                            }),
                      &lines);
    expect(rc == PAM_SUCCESS, "status lines: exit code still mapped");
    expect(lines.size() == 2 && lines[0] == "INFO looking" && lines[1] == "ERROR covered",
           "status lines relayed whole, unterminated tail dropped");
  }

  {
    // A grandchild keeps the write end open after the helper exits; the
    // helper's own exit status must still be returned.
    int rc = waitFake(spawn(true, [](int) {
      if (fork() == 0) {
        close(STDOUT_FILENO);  // or ctest waits for it too
        close(STDERR_FILENO);
        sleep(3);
        _exit(0);
      }
      return 2;
    }));
    expect(rc == PAM_IGNORE, "pipe held by a grandchild: helper exit still mapped");
  }

  {
    // Callers that set SIGCHLD to SIG_IGN get children reaped by the kernel,
    // so waitpid() fails with ECHILD: the outcome is unknown, never success.
    struct sigaction old_sa;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, &old_sa);
    int with_pipe = exitWith(0);
    int without_pipe = waitFake(spawn(false, [](int) { return 0; }));
    sigaction(SIGCHLD, &old_sa, nullptr);
    expect(with_pipe == PAM_AUTH_ERR, "SIGCHLD ignored, status pipe: exit 0 -> PAM_AUTH_ERR");
    expect(without_pipe == PAM_AUTH_ERR, "SIGCHLD ignored, no pipe: exit 0 -> PAM_AUTH_ERR");
  }

  // A hung helper (camera or fprintd stuck) must not hold the login.
  expectKilled(spawn(true, hang), "hung helper");
  expectKilled(spawn(false, hang), "hung helper, no status pipe");
  {
    std::vector<std::string> lines;
    expectKilled(spawn(true,
                       [](int fd) {
                         for (;;) {
                           writeAll(fd, "INFO still looking\n");
                           usleep(10 * 1000);
                         }
                         return 0;
                       }),
                 "hung helper still writing status lines", &lines);
    expect(!lines.empty() && lines[0] == "INFO still looking",
           "lines from a hung helper are relayed until the deadline");
  }
  {
    // A helper that exits after closing its status pipe early.
    int rc = waitFake(spawn(true, [](int fd) {
      close(fd);
      usleep(100 * 1000);
      return 0;
    }));
    expect(rc == PAM_SUCCESS, "status pipe closed early: later exit still mapped");
  }
  {
    // Relaying runs in the PAM caller: if it throws, nothing may escape, and
    // the helper must not keep running unwatched.
    Fake f = spawn(true, [](int fd) {
      writeAll(fd, "INFO hi\n");
      for (;;) pause();
      return 0;
    });
    int rc = biopass::waitForHelper(f.pid, f.fd, milliseconds(5000),
                                    [](const std::string&) { throw std::bad_alloc(); });
    expect(rc == PAM_AUTH_ERR, "relay throws -> PAM_AUTH_ERR");
    expect(waitpid(f.pid, nullptr, WNOHANG) < 0 && errno == ECHILD,
           "relay throws: helper killed and reaped");
  }

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
