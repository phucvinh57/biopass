#include "face_as.h"

#include <algorithm>
#include <cmath>

namespace biopass {

namespace {

int argmax(const float* data, int size) {
  int max_index = 0;
  float max_val = data[0];
  for (int i = 1; i < size; i++) {
    if (data[i] > max_val) {
      max_val = data[i];
      max_index = i;
    }
  }
  return max_index;
}

}  // namespace

std::optional<AntiSpoofArchitecture> parseAntiSpoofArchitecture(const std::string& name) {
  if (name == "mobilenetv3") {
    return AntiSpoofArchitecture::MobileNetV3;
  }
  if (name == "minifasv2") {
    return AntiSpoofArchitecture::MiniFASv2;
  }
  return std::nullopt;
}

FaceAntiSpoofing::FaceAntiSpoofing(const std::string& ckpt, AntiSpoofArchitecture architecture,
                                   int imgsz, const float threshold)
    : threshold(threshold),
      imgsz(imgsz),
      architecture(architecture),
      session(ckpt, "FaceAntiSpoofing") {}

std::vector<float> FaceAntiSpoofing::preprocessMobileNetV3(const ImageRGB& image) {
  const float mean[3] = {0.5931f, 0.4690f, 0.4229f};
  const float std[3] = {0.2471f, 0.2214f, 0.2157f};
  ImageRGB resize_img = resizeImage(image, this->imgsz, this->imgsz);

  return imageToChwNormalized(resize_img, mean, std);
}

std::vector<float> FaceAntiSpoofing::preprocessMiniFASv2(const ImageRGB& image) {
  ImageRGB letterboxed = imageLetterboxReflect101(image, this->imgsz);
  return imageToChw(letterboxed);
}

std::vector<float> FaceAntiSpoofing::preprocess(const ImageRGB& image) {
  if (this->architecture == AntiSpoofArchitecture::MobileNetV3) {
    return this->preprocessMobileNetV3(image);
  }
  return this->preprocessMiniFASv2(image);
}

SpoofResult FaceAntiSpoofing::inference(const ImageRGB& image) {
  std::vector<float> input_data = this->preprocess(image);

  std::vector<int64_t> input_shape = {1, 3, (int64_t)this->imgsz, (int64_t)this->imgsz};
  auto output_tensors = this->session.run(input_data, input_shape);

  const float* logits = output_tensors[0].GetTensorData<float>();

  if (this->architecture == AntiSpoofArchitecture::MobileNetV3) {
    // Index 0: Spoof, Index 1: Real
    int spoof_cls = argmax(logits, 2);
    float score = logits[spoof_cls];
    return SpoofResult(score, spoof_cls == 0 && score >= this->threshold);
  }

  // minifasv2: logits[0] = real_logit, logits[1] = spoof_logit
  float max_logit = std::max(logits[0], logits[1]);
  float exp_real = std::exp(logits[0] - max_logit);
  float exp_spoof = std::exp(logits[1] - max_logit);
  float spoof_prob = exp_spoof / (exp_real + exp_spoof);
  return SpoofResult(spoof_prob, spoof_prob >= this->threshold);
}

}  // namespace biopass
