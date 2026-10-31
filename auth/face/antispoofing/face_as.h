#ifndef FACE_AS_H
#define FACE_AS_H

#include <optional>
#include <string>
#include <vector>

#include "face_detection.h"
#include "image_utils.h"
#include "onnx_session.h"

namespace biopass {

struct SpoofResult {
  float score;
  bool spoof;
  SpoofResult(float score, bool spoof) : score(score), spoof(spoof) {}
};

// Selects preprocessing and output decoding; stored per model in biopass.db.
enum class AntiSpoofArchitecture { MobileNetV3, MiniFASv2 };

// Parses the `architecture` column value ("mobilenetv3", "minifasv2").
std::optional<AntiSpoofArchitecture> parseAntiSpoofArchitecture(const std::string& name);

class FaceAntiSpoofing {
 public:
  // imgsz is only used when the model's input height/width are dynamic.
  FaceAntiSpoofing(const std::string& ckpt, AntiSpoofArchitecture architecture, int imgsz = 128,
                   const float threshold = 0.8);

  // frame: the full camera frame; face_box: the detected face in it.
  SpoofResult inference(const ImageRGB& frame, const Box& face_box);

 private:
  std::vector<float> preprocess(const ImageRGB& frame, const Box& face_box);
  std::vector<float> preprocessMobileNetV3(const ImageRGB& face);
  std::vector<float> preprocessMiniFASv2(const ImageRGB& frame, const Box& face_box);

  float threshold;
  int imgsz;
  AntiSpoofArchitecture architecture;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_AS_H
