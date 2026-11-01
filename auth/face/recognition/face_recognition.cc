#include "face_recognition.h"

#include <cmath>
#include <stdexcept>

#include "resize.h"
#include "tensor.h"

namespace biopass {

FaceRecognition::FaceRecognition(const ModelBytes& model, int imgsz, const float threshold)
    : threshold(threshold), imgsz(imgsz), session(model, "FaceRecognition") {}

std::vector<float> FaceRecognition::preprocess(const ImageRGB& input_image) {
  ImageRGB resize_img = imageResizePad(input_image, this->imgsz, this->imgsz);

  const float mean[3] = {0.5f, 0.5f, 0.5f};
  const float std[3] = {0.5f, 0.5f, 0.5f};

  return imageToChwNormalized(resize_img, mean, std);
}

std::vector<float> FaceRecognition::inference(const ImageRGB& image) {
  std::vector<float> input_data = this->preprocess(image);

  std::vector<int64_t> input_shape = {1, 3, (int64_t)this->imgsz, (int64_t)this->imgsz};
  auto output_tensors = this->session.run(input_data, input_shape);

  auto& out = output_tensors[0];
  auto shape = out.GetTensorTypeAndShapeInfo().GetShape();
  int embed_dim = static_cast<int>(shape[1]);
  const float* data = out.GetTensorData<float>();

  return std::vector<float>(data, data + embed_dim);
}

float cosineSimilarity(const std::vector<float>& feat1, const std::vector<float>& feat2) {
  float dot_product = 0, norm1 = 0, norm2 = 0;
  for (size_t i = 0; i < feat1.size(); i++) {
    dot_product += feat1[i] * feat2[i];
    norm1 += feat1[i] * feat1[i];
    norm2 += feat2[i] * feat2[i];
  }
  norm1 = std::sqrt(norm1);
  norm2 = std::sqrt(norm2);

  if (norm1 == 0 || norm2 == 0) {
    throw std::runtime_error("One of the tensors has zero magnitude.");
  }

  float sim = dot_product / (norm1 * norm2);
  return sim;
}

MatchResult FaceRecognition::match(const ImageRGB& image1, const ImageRGB& image2) {
  std::vector<float> feature1 = this->inference(image1);
  std::vector<float> feature2 = this->inference(image2);
  const float similarity = cosineSimilarity(feature1, feature2);
  return MatchResult(similarity, isSameFace(similarity, this->threshold));
}

}  // namespace biopass
