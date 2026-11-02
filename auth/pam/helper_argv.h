#pragma once

#include <string>
#include <vector>

namespace biopass {

// argv for `biopass-helper auth`. Each value is joined to its option
// (`--username=<name>`) so no value, however it looks, can be parsed as an
// option of its own. `service` may be null or empty; a negative fd = none.
inline std::vector<std::string> buildHelperArgv(const char* username, const char* service,
                                                int status_fd, int authtok_fd = -1) {
  std::vector<std::string> args{"biopass-helper", "auth", std::string("--username=") + username};
  if (service != nullptr && service[0] != '\0') {
    args.push_back(std::string("--service=") + service);
  }
  if (status_fd >= 0) {
    args.push_back("--status-fd=" + std::to_string(status_fd));
  }
  if (authtok_fd >= 0) {
    args.push_back("--authtok-fd=" + std::to_string(authtok_fd));
  }
  return args;
}

}  // namespace biopass
