#ifndef FACE_DET_H
#define FACE_DET_H

// CPP native
#include <string>
#include <vector>

// Image utilities (replaces OpenCV)
#include <array>

#include "image_utils.h"
#include "onnx_session.h"

namespace biopass {

struct Box {
  int x1, y1, x2, y2;
  Box(int x1 = 0, int y1 = 0, int x2 = 0, int y2 = 0) : x1(x1), y1(y1), x2(x2), y2(y2) {}
};

struct Detection {
  int class_id{-1};
  Box box;
  ImageRGB image;    // raw bounding-box crop (anti-spoofing, debug images)
  ImageRGB aligned;  // 112x112 landmark-aligned face for recognition; empty if unavailable
  std::array<float, 10> landmarks{};  // 5 x (x,y) in full-image coords
  bool has_landmarks{false};
  float conf{0.0};
  std::string class_name;

  // Face image to feed the recognizer / store at enrolment.
  const ImageRGB& recognitionFace() const { return aligned.empty() ? image : aligned; }

  Detection(int class_id, std::string class_name, float conf, Box box, const ImageRGB& image)
      : class_id(class_id), class_name(class_name), conf(conf), box(box), image(image) {}

  int area() const { return (box.x2 - box.x1) * (box.y2 - box.y1); }

  bool operator>(const Detection& obj) const { return area() > obj.area(); }

  bool operator<(const Detection& obj) const { return area() < obj.area(); }
};

class FaceDetection {
 public:
  FaceDetection(const std::string& ckpt, int imgsz = 640, const float conf = 0.50,
                const float iou = 0.50);

  std::vector<Detection> inference(const ImageRGB& image);

 private:
  std::vector<float> preprocess(const ImageRGB& image);

  float conf;
  float iou;
  int imgsz;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_DET_H
