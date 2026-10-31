#ifndef FACE_DET_H
#define FACE_DET_H

#include <string>
#include <vector>

#include "image.h"
#include "onnx_session.h"

namespace biopass {

struct Box {
  int x1, y1, x2, y2;
  Box(int x1 = 0, int y1 = 0, int x2 = 0, int y2 = 0) : x1(x1), y1(y1), x2(x2), y2(y2) {}
};

struct Detection {
  int class_id{-1};
  Box box;
  ImageRGB image;
  float conf{0.0};
  std::string class_name;

  Detection(int class_id, std::string class_name, float conf, Box box, const ImageRGB& image)
      : class_id(class_id), class_name(class_name), conf(conf), box(box), image(image) {}

  int area() const { return (box.x2 - box.x1) * (box.y2 - box.y1); }

  bool operator>(const Detection& obj) const { return area() > obj.area(); }

  bool operator<(const Detection& obj) const { return area() < obj.area(); }
};

class FaceDetection {
 public:
  FaceDetection(const ModelBytes& model, int imgsz = 640, const float conf = 0.50,
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
