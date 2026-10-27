#include <spdlog/sinks/basic_file_sink.h>
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
#include "image_utils.h"
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

// This process's stdout/stderr are inherited from whatever spawned the PAM
// stack, and PAM callers routinely treat those fds as a structured control
// channel rather than a log stream -- e.g. GNOME Shell's polkit auth agent
// reads polkit-agent-helper-1's inherited output as a strict line protocol
// (SUCCESS / FAILURE / PAM_PROMPT_ECHO_OFF ...). biopass-helper is forked
// from that same process tree, so *any* line it writes there, even a single
// warning, is garbage to that parser and derails the caller's state machine
// (observed as GNOME Shell logging "Unknown line ... from helper" and
// retrying authentication in a tight loop -- `pkexec id` never returning).
// So: never write to stdout/stderr here, regardless of level. Verbose output
// only goes to a per-day file, and only when Debug Mode is on.
void setupBiopassLogger(const std::string& username, bool debug) {
  std::vector<spdlog::sink_ptr> sinks;

  if (debug) {
    biopass::setupConfig(username);
    const std::string log_path = biopass::getLogPath(username) + "/" + todayDateString() + ".log";
    try {
      auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path,
                                                                           /*truncate=*/false);
      sinks.push_back(file_sink);
      biopass::fixOwnership(log_path, username);
    } catch (const spdlog::spdlog_ex&) {
      // Can't log this failure anywhere safe (see above) -- fall through
      // with no sinks; log calls become no-ops rather than risk stdout/stderr.
    }
  }

  auto logger = std::make_shared<spdlog::logger>("biopass", sinks.begin(), sinks.end());
  spdlog::set_default_logger(logger);
  spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::off);
}

}  // namespace

int cropFace(const std::string& inputPath, const std::string& outputPath,
             const std::string& modelPath) {
  ImageRGB image = readImage(inputPath);
  if (image.empty()) {
    spdlog::error("Could not read input image: {}", inputPath);
    return 1;
  }

  std::unique_ptr<FaceDetection> faceDetector;
  try {
    faceDetector = std::make_unique<FaceDetection>(modelPath);
  } catch (const std::exception& e) {
    spdlog::error("Failed to load detection model: {}", e.what());
    return 1;
  }

  std::vector<Detection> detectedFaces = faceDetector->inference(image);
  if (detectedFaces.empty()) {
    spdlog::error("No face detected in the image");
    return 2;  // Special exit code for "no face detected"
  }

  ImageRGB faceCrop = detectedFaces[0].recognitionFace();
  if (!saveImage(outputPath, faceCrop)) {
    spdlog::error("Could not save cropped image to: {}", outputPath);
    return 1;
  }

  spdlog::info("Successfully cropped face and saved to: {}", outputPath);
  return 0;
}

// Long-running session that streams JPEG frames to stdout and accepts
// commands on stdin. Protocol (line-based for commands, length-prefixed for
// frame payloads):
//
//   FRAME              -> server captures a frame, encodes JPEG, and writes:
//                         OK <byte_count>\n<bytes>
//                         or  ERR <message>\n
//   CAPTURE <path>     -> server captures, runs face detection + crop, saves
//                         to <path>. Writes OK\n / NO_FACE\n / ERR <msg>\n
//   QUIT               -> server cleans up and exits
//
// The session keeps the camera open across commands so streaming preview and
// user-triggered capture share the same V4L2 handle (cameras are usually
// single-consumer).
int previewSession(const std::string& cameraPath, const std::string& modelPath, int jpegQuality) {
  std::optional<std::string> deviceOpt;
  if (!cameraPath.empty()) {
    deviceOpt = cameraPath;
  }

  // Silence libcamera's info-level setup noise while we set up.
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
      const std::string savedPath = biopass::tagEnrolmentPath(
          outPath, session->isGrey() ? biopass::SensorKind::Ir : biopass::SensorKind::Rgb);
      if (!saveImage(savedPath, faces[0].recognitionFace())) {
        std::cout << "ERR save failed\n" << std::flush;
        continue;
      }
      // "OK <path>": the path may differ from the request by the sensor tag.
      std::cout << "OK " << savedPath << "\n" << std::flush;
      continue;
    }

    std::cout << "ERR unknown command\n" << std::flush;
  }

  return 0;
}

int captureAndCropFace(const std::string& cameraPath, const std::string& outputPath,
                       const std::string& modelPath) {
  std::optional<std::string> deviceOpt;
  if (!cameraPath.empty()) {
    deviceOpt = cameraPath;
  }

  // Silence libcamera's info-level setup noise during enumeration; errors
  // still propagate.
  spdlog::set_level(spdlog::level::err);
  auto session = biopass::openCameraSession(deviceOpt);
  ImageRGB image = session ? session->capture() : ImageRGB{};
  spdlog::set_level(spdlog::level::info);
  if (image.empty()) {
    spdlog::error("Failed to capture image from camera: {}",
                  cameraPath.empty() ? "<auto>" : cameraPath);
    return 1;
  }
  const biopass::SensorKind sensor =
      session->isGrey() ? biopass::SensorKind::Ir : biopass::SensorKind::Rgb;

  std::unique_ptr<FaceDetection> faceDetector;
  try {
    faceDetector = std::make_unique<FaceDetection>(modelPath);
  } catch (const std::exception& e) {
    spdlog::error("Failed to load detection model: {}", e.what());
    return 1;
  }

  std::vector<Detection> detectedFaces = faceDetector->inference(image);
  if (detectedFaces.empty()) {
    spdlog::error("No face detected in the captured frame");
    return 2;
  }

  ImageRGB faceCrop = detectedFaces[0].recognitionFace();
  const std::string savedPath = biopass::tagEnrolmentPath(outputPath, sensor);
  if (!saveImage(savedPath, faceCrop)) {
    spdlog::error("Could not save cropped image to: {}", savedPath);
    return 1;
  }

  spdlog::info("Successfully captured + cropped face and saved to: {}", savedPath);
  // Machine-readable line for the desktop app (the path may carry a sensor tag).
  std::cout << "SAVED " << savedPath << "\n" << std::flush;
  return 0;
}

int authenticate(const std::string& username, const std::string& service) {
  const char* pUsername = username.c_str();

  if (!biopass::configExists(pUsername)) {
    // User has not configured biopass — skip this module transparently
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
  runtime_config.antispoof = config.methods.face.anti_spoofing.enable ||
                             (config.methods.face.anti_spoofing.ir_camera.has_value() &&
                              !config.methods.face.anti_spoofing.ir_camera->empty());

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

  // If no methods are enabled, ignore this module and let PAM jump to the next one
  if (numOfMethods == 0) {
    return 2;  // PAM_IGNORE
  }

  int retval = manager.authenticate(pUsername);

  if (retval == 0 /* PAM_SUCCESS is usually 0 */) {
    return 0;  // PAM_SUCCESS
  } else {
    return 1;  // PAM_AUTH_ERR
  }
}

int main(int argc, char** argv) {
  CLI::App app{"Biopass Helper Tool"};
  app.set_version_flag("--version,-v", BIOPASS_VERSION);
  app.require_subcommand(1, 1);

  auto crop_cmd = app.add_subcommand("crop-face", "Crop a face from an image");
  std::string inputPath, outputPath, modelPath;
  crop_cmd->add_option("--input,-i", inputPath, "Input image path")->required();
  crop_cmd->add_option("--output,-o", outputPath, "Output image path")->required();
  crop_cmd->add_option("--model,-m", modelPath, "Detection model path")->required();

  auto capture_cmd = app.add_subcommand("capture-face",
                                        "Capture a frame from a camera and crop the detected face");
  std::string capCameraPath, capOutputPath, capModelPath;
  capture_cmd->add_option("--camera,-c", capCameraPath,
                          "Camera device path (e.g. /dev/video0). Empty = auto-select first.");
  capture_cmd->add_option("--output,-o", capOutputPath, "Output image path")->required();
  capture_cmd->add_option("--model,-m", capModelPath, "Detection model path")->required();

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
  auto auth_cmd = app.add_subcommand("auth", "Authenticate a user with Biopass");
  auth_cmd->add_option("--username,-u", username, "Username for authentication")->required();
  auth_cmd->add_option("--service,-s", pamService, "PAM service name");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  if (app.got_subcommand(crop_cmd)) {
    return cropFace(inputPath, outputPath, modelPath);
  }

  if (app.got_subcommand(capture_cmd)) {
    return captureAndCropFace(capCameraPath, capOutputPath, capModelPath);
  }

  if (app.got_subcommand(preview_cmd)) {
    return previewSession(previewCameraPath, previewModelPath, previewQuality);
  }

  if (app.got_subcommand(auth_cmd)) {
    if (username.empty()) {
      spdlog::info("{}", app.help());
      return 2;  // PAM_IGNORE logic / error
    }
    return authenticate(username, pamService);
  }

  spdlog::error("No valid subcommand provided");
  return 1;
}
