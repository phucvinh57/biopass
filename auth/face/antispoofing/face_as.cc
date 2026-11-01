#include "face_as.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

#include "antispoof_output.h"
#include "resize.h"
#include "tensor.h"

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

FaceAntiSpoofing::FaceAntiSpoofing(const ModelBytes& model, AntiSpoofArchitecture architecture,
                                   int imgsz, const float threshold)
    : threshold(threshold),
      imgsz(imgsz),
      architecture(architecture),
      session(model, "FaceAntiSpoofing") {
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
  const size_t count = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();

  if (this->architecture == AntiSpoofArchitecture::MobileNetV3) {
    // The bundled mobilenetv3_antispoof.onnx ends in a Softmax; its output
    // tensor is named "logits" but already holds probabilities. User-imported
    // models are labelled mobilenetv3 too, so check rather than assume.
    const AntiSpoofDecision decision = decodeMobileNetV3Output(logits, count, this->threshold);
    if (!decision.valid) {
      spdlog::error(
          "FaceAntiSpoofing: MobileNetV3 output is not 2 probabilities ({} values); the model "
          "must end in Softmax. Treating face as spoof.",
          count);
    }
    return SpoofResult(decision.score, decision.spoof);
  }

  const AntiSpoofDecision decision = decodeMiniFasV2Output(logits, count, this->threshold);
  if (!decision.valid) {
    spdlog::error(
        "FaceAntiSpoofing: MiniFASv2 output is not 2 finite logits ({} values). Treating face as "
        "spoof.",
        count);
  }
  return SpoofResult(decision.score, decision.spoof);
}

}  // namespace biopass
