#ifndef FACE_AS_H
#define FACE_AS_H

#include <string>
#include <vector>

// Image utilities (replaces OpenCV)
#include "image_utils.h"
#include "onnx_session.h"

namespace biopass {

struct SpoofResult {
  float score;
  bool spoof;
  SpoofResult(float score, bool spoof) : score(score), spoof(spoof) {}
};

// model_type: "minifasv2" or "mobilenetv3". Anything else (including the
// default "") picks the type from the checkpoint filename: "minifas" in the
// name means MiniFASv2, otherwise MobileNetV3.
class FaceAntiSpoofing {
 public:
  FaceAntiSpoofing(const std::string& ckpt, int imgsz = 128, const float threshold = 0.8,
                   const std::string& model_type = "");

  SpoofResult inference(const ImageRGB& image);

 private:
  std::vector<float> preprocess(const ImageRGB& image);
  std::vector<float> preprocessMobileNetV3(const ImageRGB& image);
  std::vector<float> preprocessMiniFASv2(const ImageRGB& image);

  float threshold;
  int imgsz;
  std::string model_type;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_AS_H
