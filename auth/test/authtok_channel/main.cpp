// Checks the helper -> PAM module password channel (pam/authtok_channel.h):
// only 1..128 bytes without NUL get through, and a write end left open
// never blocks the reader.

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "authtok_channel.h"

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
  if (!cond) {
    g_failures++;
  }
}

int finish() {
  if (g_failures != 0) {
    std::printf("%d FAILED\n", g_failures);
    return 1;
  }
  std::printf("ALL PASS\n");
  return 0;
}

}  // namespace

// Writes `data` into a fresh pipe, closes the write end unless `keep_open`,
// and reads it back. Returns the length read (0 = refused).
size_t roundTrip(const std::string& data, bool keep_open, std::string* out) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    return 0;
  }
  biopass::writeAuthtok(fds[1], data.data(), data.size());
  if (!keep_open) {
    close(fds[1]);
  }
  char buf[biopass::kMaxAuthtokLen + 2];
  const size_t n = biopass::readAuthtok(fds[0], buf, sizeof(buf));
  if (out != nullptr) {
    *out = n > 0 ? std::string(buf, n) : std::string();
  }
  close(fds[0]);
  if (keep_open) {
    close(fds[1]);
  }
  return n;
}

int main() {
  std::string got;
  expect(roundTrip("hunter2", false, &got) == 7 && got == "hunter2", "short password");
  expect(roundTrip("x", false, &got) == 1 && got == "x", "1 byte");
  const std::string max(biopass::kMaxAuthtokLen, 'a');
  expect(roundTrip(max, false, &got) == max.size() && got == max, "128 bytes");
  expect(roundTrip(max + "a", false, nullptr) == 0, "129 bytes refused");
  expect(roundTrip("", false, nullptr) == 0, "empty refused");
  expect(roundTrip(std::string("ab\0cd", 5), false, nullptr) == 0, "NUL refused");
  expect(roundTrip("p\xc3\xa4ss word\n", false, &got) == 11, "UTF-8, space and newline kept");
  expect(roundTrip("open", true, &got) == 4 && got == "open", "write end left open: no hang");
  expect(roundTrip("", true, nullptr) == 0, "nothing written, write end open: no hang");

  char small[8];
  int fds[2];
  expect(pipe(fds) == 0, "pipe");
  biopass::writeAuthtok(fds[1], "abc", 3);
  close(fds[1]);
  expect(biopass::readAuthtok(fds[0], small, sizeof(small)) == 0, "too-small buffer refused");
  close(fds[0]);

  const std::string over = max + "a";
  expect(biopass::isValidAuthtok("a", 1) && !biopass::isValidAuthtok("a", 0) &&
             !biopass::isValidAuthtok(over.data(), over.size()),
         "isValidAuthtok bounds");
  return finish();
}
