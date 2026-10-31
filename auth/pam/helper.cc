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
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "auth_config.h"
#include "auth_deadline.h"
#include "auth_manager.h"
#include "common/camera_capture.h"
#include "detection/face_detection.h"
#include "face_auth.h"
#include "fingerprint_auth.h"
#include "fs_user.h"
#include "helper_exit.h"
#include "image_io.h"
#include "status_reporter.h"
#include "user_paths.h"

using biopass::Detection;
using biopass::FaceDetection;

namespace {

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

  const std::optional<biopass::UserPaths> user =
      debug ? biopass::resolveUser(username) : std::nullopt;
  if (user) {
    // The log lives in a user-writable dir: open it as the user so a planted
    // symlink can't make root append to (or create) a file elsewhere. The
    // sink keeps the fd, so later writes need no guard.
    biopass::ScopedFsUser as_user(user->uid, user->gid);
    if (as_user.ok() && biopass::setupDataDirs(user->data_dir) == 0) {
      const std::string log_path = user->logsDir() + "/" + todayDateString() + ".log";
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
      faceDetector = std::make_unique<FaceDetection>(biopass::loadModelFile(modelPath));
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

biopass::HelperExit authenticate(const std::string& username, const std::string& service) {
  using biopass::HelperExit;
  const char* pUsername = username.c_str();
  // Counted from here so config and model loading come out of the budget too.
  const auto deadline = std::chrono::steady_clock::now() + biopass::kAuthBudget;

  const std::optional<biopass::BiopassConfig> loaded = biopass::readConfig(pUsername);
  if (!loaded) {
    return HelperExit::Ignore;  // no usable config.yaml
  }
  const biopass::BiopassConfig& config = *loaded;

  if (!service.empty() &&
      std::find(config.strategy.ignore_services.begin(), config.strategy.ignore_services.end(),
                service) != config.strategy.ignore_services.end()) {
    return HelperExit::Ignore;
  }

  setupBiopassLogger(pUsername, config.strategy.debug);

  biopass::AuthConfig runtime_config;
  runtime_config.debug = config.strategy.debug;

  biopass::AuthManager manager;
  manager.setMode(config.strategy.execution_mode == "sequential"
                      ? biopass::ExecutionMode::Sequential
                      : biopass::ExecutionMode::Parallel);
  manager.setConfig(runtime_config);
  manager.setDeadline(deadline);

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
    return HelperExit::Ignore;
  }

  return biopass::toHelperExit(manager.authenticate(pUsername));
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
    // CLI11 exits 0 for --help/--version, which the PAM module would read as
    // a successful authentication, so every parse stop is an error here.
    // Text goes to stderr, never stdout (the PAM caller's channel), and the
    // auth path prints nothing at all.
    const bool authMode = argc > 1 && std::string(argv[1]) == "auth";
    if (!authMode) {
      app.exit(e, std::cerr, std::cerr);
    }
    return biopass::exitCode(biopass::HelperExit::AuthErr);
  }

  if (app.got_subcommand(preview_cmd)) {
    return previewSession(previewCameraPath, previewModelPath, previewQuality);
  }

  if (app.got_subcommand(auth_cmd)) {
    // spdlog's default logger writes to stdout, which belongs to the PAM
    // caller (see setupBiopassLogger). Silence it before anything can log;
    // setupBiopassLogger replaces it once the config is known.
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("biopass"));
    spdlog::set_level(spdlog::level::off);
    if (username.empty()) {
      spdlog::info("{}", app.help());
      return biopass::exitCode(biopass::HelperExit::Ignore);
    }
    // ScopedFsUser switches only the filesystem uid/gid; root's supplementary
    // groups would still grant group access. Drop them before any thread
    // starts. Root keeps its capabilities, so nothing else changes.
    if (geteuid() == 0 && setgroups(0, nullptr) != 0) {
      return biopass::exitCode(biopass::HelperExit::AuthErr);  // fail closed
    }
    // Only methods that start scanning report, so skipped runs stay silent.
    biopass::setStatusFd(statusFd);
    // An unexpected exception (config, model loading, thread start) would
    // otherwise abort with a core dump. Fail closed with AuthErr instead.
    try {
      return biopass::exitCode(authenticate(username, pamService));
    } catch (const std::exception& e) {
      spdlog::error("Biopass: authentication threw: {}", e.what());
    } catch (...) {
      spdlog::error("Biopass: authentication threw");
    }
    return biopass::exitCode(biopass::HelperExit::AuthErr);
  }

  spdlog::error("No valid subcommand provided");
  return 1;
}
