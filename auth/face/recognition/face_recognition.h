#ifndef FACE_REG_H
#define FACE_REG_H

#include <string>
#include <vector>

#include "image_utils.h"
#include "onnx_session.h"

namespace biopass {

struct MatchResult {
  float dist;
  bool similar;
  MatchResult(float dist, bool similar) : dist(dist), similar(similar) {}
};

class FaceRecognition {
 public:
  FaceRecognition(const ModelBytes& model, int imgsz = 112, const float threshold = 0.50);

  MatchResult match(const ImageRGB& image1, const ImageRGB& image2);

 private:
  std::vector<float> inference(const ImageRGB& image);
  std::vector<float> preprocess(const ImageRGB& image);
  float cosine(const std::vector<float>& feat1, const std::vector<float>& feat2);

  float threshold;
  int imgsz;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_REG_H
