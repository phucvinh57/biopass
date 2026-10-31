#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "face_as.h"
#include "face_detection.h"
#include "image_utils.h"

using biopass::AntiSpoofArchitecture;
using biopass::Detection;
using biopass::FaceAntiSpoofing;
using biopass::FaceDetection;
using biopass::SpoofResult;

namespace {

std::string default_model_dir() {
#ifdef BIOPASS_MODEL_DIR
  return BIOPASS_MODEL_DIR;
#else
  return "./face/models";
#endif
}

}  // namespace

int main(int argc, char** argv) {
  const std::string model_dir = default_model_dir();
  std::vector<std::string> image_paths;
  std::string detector_model = model_dir + "/yolov8n-face.onnx";
  std::string antispoof_model = model_dir + "/minifas_v2.onnx";
  std::string architecture_name = "minifasv2";
  float threshold = 0.8f;

  CLI::App app("Run Biopass face detection + AI anti-spoofing on full images.");
  app.add_option("images", image_paths, "Paths to full (uncropped) images.")->required();
  app.add_option("--det-model", detector_model, "Path to face detection ONNX model.");
  app.add_option("--as-model", antispoof_model, "Path to anti-spoofing ONNX model.");
  app.add_option("--arch", architecture_name,
                 "Anti-spoofing architecture (minifasv2|mobilenetv3).");
  app.add_option("--threshold", threshold, "Spoof threshold (default: 0.8).");

  CLI11_PARSE(app, argc, argv);

  const auto architecture = biopass::parseAntiSpoofArchitecture(architecture_name);
  if (!architecture) {
    std::cerr << "Error: unknown architecture: " << architecture_name << std::endl;
    return 1;
  }

  try {
    FaceDetection detector(biopass::loadModelFile(detector_model));
    FaceAntiSpoofing face_as(biopass::loadModelFile(antispoof_model), *architecture, 128,
                             threshold);

    for (const auto& path : image_paths) {
      ImageRGB frame = readImage(path);
      if (frame.empty()) {
        std::cout << path << ": failed to load" << std::endl;
        continue;
      }
      std::vector<Detection> detections = detector.inference(frame);
      if (detections.empty()) {
        std::cout << path << ": no face" << std::endl;
        continue;
      }
      const auto& box = detections.front().box;
      SpoofResult result = face_as.inference(frame, box);
      std::cout << path << ": box=(" << box.x1 << "," << box.y1 << "," << box.x2 << "," << box.y2
                << ") score=" << result.score << " " << (result.spoof ? "SPOOF" : "REAL")
                << std::endl;
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
