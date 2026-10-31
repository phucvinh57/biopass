#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "face_detection.h"
#include "face_recognition.h"
#include "image_io.h"

using biopass::Detection;
using biopass::FaceDetection;
using biopass::FaceRecognition;
using biopass::MatchResult;

namespace {

std::string default_model_dir() {
#ifdef BIOPASS_MODEL_DIR
  return BIOPASS_MODEL_DIR;
#else
  return "./face/models";
#endif
}

ImageRGB extract_largest_face(FaceDetection& detector, const std::string& image_path,
                              const std::string& image_label) {
  ImageRGB input_image = readImage(image_path);
  if (input_image.empty()) {
    throw std::runtime_error("Failed to load image: " + image_path);
  }

  std::vector<Detection> detections = detector.inference(input_image);
  if (detections.empty()) {
    throw std::runtime_error("No face detected in " + image_label + ": " + image_path);
  }

  std::cout << image_label << ": detected " << detections.size()
            << " face(s), using the largest face for comparison." << std::endl;
  return detections.front().image;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string model_dir = default_model_dir();
  std::string image_1_path;
  std::string image_2_path;
  std::string detector_model = model_dir + "/yolov8n-face.onnx";
  std::string recognition_model = model_dir + "/edgeface_s_gamma_05.onnx";
  float threshold = 0.8f;
  bool save_crops = false;
  std::string crop_1_path = "result1.jpg";
  std::string crop_2_path = "result2.jpg";

  CLI::App app("Compare two face images using Biopass face detection and recognition.");
  app.add_option("image1", image_1_path, "Path to the first image.")->required();
  app.add_option("image2", image_2_path, "Path to the second image.")->required();
  app.add_option("--det-model", detector_model, "Path to face detection ONNX model.");
  app.add_option("--reg-model", recognition_model, "Path to face recognition ONNX model.");
  app.add_option("--threshold", threshold,
                 "Similarity threshold used by face recognition (default: 0.8).");
  app.add_flag("--save-crops", save_crops,
               "Save detected largest face crops to --crop1 and --crop2 paths.");
  app.add_option("--crop1", crop_1_path, "Output path for cropped face from image1.");
  app.add_option("--crop2", crop_2_path, "Output path for cropped face from image2.");

  CLI11_PARSE(app, argc, argv);

  try {
    FaceDetection detector(biopass::loadModelFile(detector_model));
    FaceRecognition recognizer(biopass::loadModelFile(recognition_model), 112, threshold);

    ImageRGB face1 = extract_largest_face(detector, image_1_path, "Image1");
    ImageRGB face2 = extract_largest_face(detector, image_2_path, "Image2");

    if (save_crops) {
      if (!saveImage(crop_1_path, face1)) {
        throw std::runtime_error("Failed to save cropped image to: " + crop_1_path);
      }
      if (!saveImage(crop_2_path, face2)) {
        throw std::runtime_error("Failed to save cropped image to: " + crop_2_path);
      }
      std::cout << "Saved crops: " << crop_1_path << ", " << crop_2_path << std::endl;
    }

    MatchResult match_result = recognizer.match(face1, face2);
    std::cout << "Similarity score: " << match_result.dist << std::endl;
    std::cout << "Match result: " << (match_result.similar ? "SAME_PERSON" : "DIFFERENT_PERSON")
              << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
