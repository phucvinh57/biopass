// Runs the PAM module's helper wait loop (pam/helper_wait.h) against fake
// helpers: forked children that exit, write status lines or hang. Exits
// non-zero on any failure.

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
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

int waitFake(const Fake& f, std::vector<std::string>* lines = nullptr) {
  return biopass::waitForHelper(f.pid, f.fd, [lines](const std::string& line) {
    if (lines != nullptr) {
      lines->push_back(line);
    }
  });
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

  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}
