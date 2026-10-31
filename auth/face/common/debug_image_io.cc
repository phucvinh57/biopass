#include "debug_image_io.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <string>

#include "auth_config.h"
#include "fs_user.h"

namespace biopass {

void saveFailedFace(const std::string& username, const ImageRGB& face, const std::string& reason) {
  // Root runs this; write as the user so a planted symlink can't redirect it.
  ScopedFsUser as_user(username);
  if (!as_user.ok() || setupConfig(username) != 0) {
    return;
  }
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  const std::string failed_face_path =
      getDebugPath(username) + "/" + reason + "." + std::to_string(ms) + ".jpg";
  if (!saveImage(failed_face_path, face)) {
    spdlog::error("FaceAuth: Could not save failed face to {}", failed_face_path);
  }
}

}  // namespace biopass
