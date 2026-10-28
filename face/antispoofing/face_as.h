#ifndef FACE_AS_H
#define FACE_AS_H

#include <optional>
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

// Selects preprocessing and output decoding. Stored per model in the
// `architecture` column of biopass.db.
enum class AntiSpoofArchitecture { MobileNetV3, MiniFASv2 };

// Parses the `architecture` column value ("mobilenetv3", "minifasv2").
std::optional<AntiSpoofArchitecture> parseAntiSpoofArchitecture(const std::string& name);

class FaceAntiSpoofing {
 public:
  FaceAntiSpoofing(const std::string& ckpt, AntiSpoofArchitecture architecture, int imgsz = 128,
                   const float threshold = 0.8);

  SpoofResult inference(const ImageRGB& image);

 private:
  std::vector<float> preprocess(const ImageRGB& image);
  std::vector<float> preprocessMobileNetV3(const ImageRGB& image);
  std::vector<float> preprocessMiniFASv2(const ImageRGB& image);

  float threshold;
  int imgsz;
  AntiSpoofArchitecture architecture;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_AS_H
