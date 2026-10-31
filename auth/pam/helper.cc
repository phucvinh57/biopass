#include <grp.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <ctime>
#include <iostream>
#include <memory>
#include <vector>

#include "auth_config.h"
#include "auth_manager.h"
#include "common/camera_capture.h"
#include "detection/face_detection.h"
#include "face_auth.h"
#include "fingerprint_auth.h"
#include "fs_user.h"
#include "image_utils.h"
#include "status_reporter.h"
#include "stb_image_write.h"

using biopass::Detection;
using biopass::FaceDetection;

namespace {

void appendJpegToBuffer(void* context, void* data, int size) {
  auto* buf = static_cast<std::vector<uint8_t>*>(context);
  buf->insert(buf->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

bool encodeJpeg(const ImageRGB& img, int quality, std::vector<uint8_t>& out) {
  out.clear();
  return stbi_write_jpg_to_func(&appendJpegToBuffer, &out, img.width, img.height, 3, img.ptr(),
                                quality) != 0;
}

std::string todayDateString() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  char buf[sizeof("yyyy-mm-dd")];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
  return std::string(buf);
}

// Never write to stdout/stderr here: they are inherited from the PAM caller,
// which may parse them as a line protocol (e.g. GNOME Shell's polkit agent
// reads polkit-agent-helper-1's output). Any stray line breaks its state
// machine and makes it retry auth in a tight loop (`pkexec id` hangs).
// Logs go only to a per-day file, and only in Debug Mode.
void setupBiopassLogger(const std::string& username, bool debug) {
  std::vector<spdlog::sink_ptr> sinks;

  if (debug) {
    // The log lives in a user-writable dir: open it as the user so a planted
    // symlink can't make root append to (or create) a file elsewhere. The
    // sink keeps the fd, so later writes need no guard.
    biopass::ScopedFsUser as_user(username);
    if (as_user.ok() && biopass::setupConfig(username) == 0) {
      const std::string log_path = biopass::getLogPath(username) + "/" + todayDateString() + ".log";
      try {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path,
                                                                            /*truncate=*/false));
      } catch (const spdlog::spdlog_ex&) {
        // Nowhere safe to report this; run with no sinks.
      }
    }
  }

  auto logger = std::make_shared<spdlog::logger>("biopass", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);
  spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::off);
}

}  // namespace

// Streams JPEG frames over stdout, reads commands on stdin:
//   FRAME          -> OK <byte_count>\n<bytes> | ERR <msg>\n
//   CAPTURE <path> -> detect + crop face to <path>: OK\n | NO_FACE\n | ERR <msg>\n
//   QUIT           -> exit
// The camera stays open across commands so preview and capture share one
// handle (cameras are usually single-consumer).
int previewSession(const std::string& cameraPath, const std::string& modelPath, int jpegQuality) {
  std::optional<std::string> deviceOpt;
  if (!cameraPath.empty()) {
    deviceOpt = cameraPath;
  }

  // stdout is the protocol channel; one stray log line desyncs it.
  spdlog::set_default_logger(std::make_shared<spdlog::logger>(
      "biopass", std::make_shared<spdlog::sinks::stderr_sink_mt>()));

  // Silence libcamera's info-level setup noise.
  spdlog::set_level(spdlog::level::err);
  auto session = biopass::openCameraSession(deviceOpt);
  spdlog::set_level(spdlog::level::info);
  if (!session || !session->isOpen()) {
    std::cout << "ERR failed to open camera\n" << std::flush;
    return 1;
  }

  std::unique_ptr<FaceDetection> faceDetector;
  if (!modelPath.empty()) {
    try {
      faceDetector = std::make_unique<FaceDetection>(modelPath);
    } catch (const std::exception& e) {
      std::cout << "ERR failed to load detection model: " << e.what() << "\n" << std::flush;
      return 1;
    }
  }

  std::cout << "READY\n" << std::flush;

  std::vector<uint8_t> jpegBuf;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "QUIT") {
      break;
    }

    if (line == "FRAME") {
      ImageRGB img = session->capture();
      if (img.empty()) {
        std::cout << "ERR capture failed\n" << std::flush;
        continue;
      }
      if (!encodeJpeg(img, jpegQuality, jpegBuf)) {
        std::cout << "ERR jpeg encode failed\n" << std::flush;
        continue;
      }
      std::cout << "OK " << jpegBuf.size() << "\n";
      std::cout.write(reinterpret_cast<const char*>(jpegBuf.data()),
                      static_cast<std::streamsize>(jpegBuf.size()));
      std::cout.flush();
      continue;
    }

    if (line.rfind("CAPTURE ", 0) == 0) {
      if (!faceDetector) {
        std::cout << "ERR detection model not loaded\n" << std::flush;
        continue;
      }
      std::string outPath = line.substr(8);
      ImageRGB img = session->capture();
      if (img.empty()) {
        std::cout << "ERR capture failed\n" << std::flush;
        continue;
      }
      auto faces = faceDetector->inference(img);
      if (faces.empty()) {
        std::cout << "NO_FACE\n" << std::flush;
        continue;
      }
      if (!saveImage(outPath, faces[0].image)) {
        std::cout << "ERR save failed\n" << std::flush;
        continue;
      }
      std::cout << "OK\n" << std::flush;
      continue;
    }

    std::cout << "ERR unknown command\n" << std::flush;
  }

  return 0;
}

int authenticate(const std::string& username, const std::string& service) {
  const char* pUsername = username.c_str();

  if (!biopass::configExists(pUsername)) {
    return 2;  // PAM_IGNORE
  }
  biopass::BiopassConfig config = biopass::readConfig(pUsername);

  if (!service.empty() &&
      std::find(config.strategy.ignore_services.begin(), config.strategy.ignore_services.end(),
                service) != config.strategy.ignore_services.end()) {
    return 2;  // PAM_IGNORE
  }

  setupBiopassLogger(pUsername, config.strategy.debug);

  biopass::AuthConfig runtime_config;
  runtime_config.debug = config.strategy.debug;

  biopass::AuthManager manager;
  manager.setMode(config.strategy.execution_mode == "sequential"
                      ? biopass::ExecutionMode::Sequential
                      : biopass::ExecutionMode::Parallel);
  manager.setConfig(runtime_config);

  int numOfMethods = 0;
  for (const auto& method_name : config.strategy.order) {
    if (method_name == "face" && config.methods.face.enable) {
      manager.addMethod(std::make_unique<biopass::FaceAuth>(config.methods.face, username));
      numOfMethods++;
    } else if (method_name == "fingerprint" && config.methods.fingerprint.enable) {
      manager.addMethod(std::make_unique<biopass::FingerprintAuth>(config.methods.fingerprint));
      numOfMethods++;
    }
  }

  if (numOfMethods == 0) {
    return 2;  // PAM_IGNORE
  }

  int retval = manager.authenticate(pUsername);

  if (retval == 0) {
    return 0;  // PAM_SUCCESS
  } else {
    return 1;  // PAM_AUTH_ERR
  }
}

int main(int argc, char** argv) {
  CLI::App app{"Biopass Helper Tool"};
  app.set_version_flag("--version,-v", BIOPASS_VERSION);
  app.require_subcommand(1, 1);

  auto preview_cmd = app.add_subcommand(
      "preview-session",
      "Long-running camera session: streams JPEG frames + handles capture via stdin/stdout");
  std::string previewCameraPath, previewModelPath;
  int previewQuality = 70;
  preview_cmd->add_option("--camera,-c", previewCameraPath,
                          "Camera device path (e.g. /dev/video0). Empty = auto-select first.");
  preview_cmd->add_option("--model,-m", previewModelPath,
                          "Detection model path (required for CAPTURE command)");
  preview_cmd->add_option("--quality,-q", previewQuality,
                          "JPEG encoding quality 1-100 (default 70)");

  std::string username;
  std::string pamService;
  int statusFd = -1;
  auto auth_cmd = app.add_subcommand("auth", "Authenticate a user with Biopass");
  auth_cmd->add_option("--username,-u", username, "Username for authentication")->required();
  auth_cmd->add_option("--service,-s", pamService, "PAM service name");
  auth_cmd->add_option("--status-fd", statusFd,
                       "Inherited fd for status lines relayed to the PAM conversation");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  if (app.got_subcommand(preview_cmd)) {
    return previewSession(previewCameraPath, previewModelPath, previewQuality);
  }

  if (app.got_subcommand(auth_cmd)) {
    if (username.empty()) {
      spdlog::info("{}", app.help());
      return 2;  // PAM_IGNORE
    }
    // ScopedFsUser switches only the filesystem uid/gid; root's supplementary
    // groups would still grant group access. Drop them before any thread
    // starts. Root keeps its capabilities, so nothing else changes.
    if (geteuid() == 0 && setgroups(0, nullptr) != 0) {
      return 1;  // PAM_AUTH_ERR: fail closed
    }
    // Only methods that start scanning report, so skipped runs stay silent.
    biopass::setStatusFd(statusFd);
    return authenticate(username, pamService);
  }

  spdlog::error("No valid subcommand provided");
  return 1;
}
