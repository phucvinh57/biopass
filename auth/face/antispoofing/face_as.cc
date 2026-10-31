#include "face_as.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "antispoof_output.h"

namespace biopass {

namespace {

// MiniFASv2 (suriAI/face-antispoof-onnx) is trained on square crops of
// 1.5 x the longer side of the face box, so it sees hair, ears and the
// face/background boundary rather than a tight face crop.
constexpr float kMiniFASv2CropScale = 1.5f;

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
      session(ckpt, "FaceAntiSpoofing") {
  // Use the model's own input size (NCHW) when it is fixed and square.
  const std::vector<int64_t> shape = this->session.inputShape();
  if (shape.size() == 4 && shape[2] > 0 && shape[2] == shape[3]) {
    this->imgsz = static_cast<int>(shape[2]);
  }
}

std::vector<float> FaceAntiSpoofing::preprocessMobileNetV3(const ImageRGB& face) {
  const float mean[3] = {0.5931f, 0.4690f, 0.4229f};
  const float std[3] = {0.2471f, 0.2214f, 0.2157f};
  ImageRGB resize_img = resizeImage(face, this->imgsz, this->imgsz);

  return imageToChwNormalized(resize_img, mean, std);
}

// Matches crop() + preprocess() in suriAI/face-antispoof-onnx: square context
// crop (reflect-101 outside the frame), resize to imgsz, RGB, [0, 1].
std::vector<float> FaceAntiSpoofing::preprocessMiniFASv2(const ImageRGB& frame,
                                                         const Box& face_box) {
  ImageRGB context = imageSquareCropReflect101(frame, face_box.x1, face_box.y1, face_box.x2,
                                               face_box.y2, kMiniFASv2CropScale);
  ImageRGB letterboxed = imageLetterboxReflect101(context, this->imgsz);
  return imageToChw(letterboxed);
}

std::vector<float> FaceAntiSpoofing::preprocess(const ImageRGB& frame, const Box& face_box) {
  if (this->architecture == AntiSpoofArchitecture::MobileNetV3) {
    return this->preprocessMobileNetV3(
        frame.crop(face_box.x1, face_box.y1, face_box.x2, face_box.y2));
  }
  return this->preprocessMiniFASv2(frame, face_box);
}

SpoofResult FaceAntiSpoofing::inference(const ImageRGB& frame, const Box& face_box) {
  std::vector<float> input_data = this->preprocess(frame, face_box);
  if (input_data.empty()) {
    throw std::runtime_error("FaceAntiSpoofing: empty face crop");
  }

  std::vector<int64_t> input_shape = {1, 3, (int64_t)this->imgsz, (int64_t)this->imgsz};
  auto output_tensors = this->session.run(input_data, input_shape);

  const float* logits = output_tensors[0].GetTensorData<float>();

  if (this->architecture == AntiSpoofArchitecture::MobileNetV3) {
    const AntiSpoofDecision decision = decodeMobileNetV3Output(logits, 2, this->threshold);
    return SpoofResult(decision.score, decision.spoof);
  }

  // minifasv2: logits[0] = real_logit, logits[1] = spoof_logit
  float max_logit = std::max(logits[0], logits[1]);
  float exp_real = std::exp(logits[0] - max_logit);
  float exp_spoof = std::exp(logits[1] - max_logit);
  float spoof_prob = exp_spoof / (exp_real + exp_spoof);
  return SpoofResult(spoof_prob, spoof_prob >= this->threshold);
}

}  // namespace biopass
