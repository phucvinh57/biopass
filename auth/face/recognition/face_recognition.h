#ifndef FACE_REG_H
#define FACE_REG_H

#include <string>
#include <vector>

#include "image.h"
#include "onnx_session.h"

namespace biopass {

// Cosine similarity of two embeddings, in [-1, 1]: higher = more alike.
// Throws std::runtime_error if either has zero magnitude.
float cosineSimilarity(const std::vector<float>& feat1, const std::vector<float>& feat2);

// Same face iff the similarity is strictly above the threshold (NaN never is).
inline bool isSameFace(float similarity, float threshold) { return similarity > threshold; }

struct MatchResult {
  // Cosine similarity of the two faces' embeddings.
  float similarity;
  bool similar;
  MatchResult(float similarity, bool similar) : similarity(similarity), similar(similar) {}
};

class FaceRecognition {
 public:
  FaceRecognition(const ModelBytes& model, int imgsz = 112, const float threshold = 0.50);

  MatchResult match(const ImageRGB& image1, const ImageRGB& image2);

 private:
  std::vector<float> inference(const ImageRGB& image);
  std::vector<float> preprocess(const ImageRGB& image);

  float threshold;
  int imgsz;
  OnnxSession session;
};

}  // namespace biopass

#endif  // FACE_REG_H
