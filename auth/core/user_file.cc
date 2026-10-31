#include "user_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs_user.h"

namespace biopass {

std::optional<std::string> readRegularFile(const std::string& path, size_t max_bytes) {
  // Cheap early refusal so most non-regular paths are never opened at all;
  // the fstat() below is what closes the race.
  struct stat st{};
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
    return std::nullopt;
  }

  const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  std::optional<std::string> result;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
      static_cast<unsigned long long>(st.st_size) <= max_bytes) {
    std::string data;
    data.reserve(static_cast<size_t>(st.st_size));
    char buf[64 * 1024];
    bool ok = true;
    for (;;) {
      const ssize_t n = read(fd, buf, sizeof(buf));
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n < 0) {
        ok = false;
        break;
      }
      if (n == 0) {
        break;
      }
      // The file may have grown since fstat().
      if (data.size() + static_cast<size_t>(n) > max_bytes) {
        ok = false;
        break;
      }
      data.append(buf, static_cast<size_t>(n));
    }
    if (ok) {
      result = std::move(data);
    }
  }
  close(fd);
  return result;
}

std::optional<std::string> readUserFile(const std::string& username, const std::string& path,
                                        size_t max_bytes) {
  ScopedFsUser as_user(username);
  if (!as_user.ok()) {
    return std::nullopt;
  }
  return readRegularFile(path, max_bytes);
}

}  // namespace biopass
