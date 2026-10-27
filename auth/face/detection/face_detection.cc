#include "face_detection.h"

#include <algorithm>

#include "face_align.h"
#include "utils.h"

namespace {
constexpr float kMinLandmarkConf = 0.5f;
}

namespace biopass {

FaceDetection::FaceDetection(const std::string& ckpt, int imgsz, const float conf, const float iou)
    : conf(conf), iou(iou), imgsz(imgsz), session(ckpt, "FaceDetection") {}

std::vector<Detection> FaceDetection::inference(const ImageRGB& image) {
  ImageRGB input_image = imageLetterbox(image, this->imgsz, this->imgsz);
  std::vector<float> image_data = this->preprocess(input_image);

  std::vector<int64_t> input_shape = {1, 3, (int64_t)this->imgsz, (int64_t)this->imgsz};
  auto output_tensors = this->session.run(image_data, input_shape);

  auto& out = output_tensors[0];
  auto shape = out.GetTensorTypeAndShapeInfo().GetShape();
  int pred_dim = static_cast<int>(shape[1]);
  int num_preds = static_cast<int>(shape[2]);
  const float* output_data = out.GetTensorData<float>();

  auto raw_dets = non_max_suppression(output_data, num_preds, pred_dim, this->conf, this->iou);
  scale_boxes({input_image.height, input_image.width}, raw_dets, {image.height, image.width});

  std::vector<Detection> results;
  for (auto& d : raw_dets) {
    int x1 = std::max(0, (int)d.x1);
    int y1 = std::max(0, (int)d.y1);
    int x2 = std::min(image.width, (int)d.x2);
    int y2 = std::min(image.height, (int)d.y2);

    // Ensure the box has positive area after clipping
    if (x2 - x1 <= 0 || y2 - y1 <= 0) {
      continue;
    }

    Box xyxy_box(x1, y1, x2, y2);
    ImageRGB crop_face = image.crop(x1, y1, x2, y2);
    Detection det(d.cls, std::string("face"), d.conf, xyxy_box, crop_face);
    if (d.has_kps) {
      bool confident = true;
      for (int k = 0; k < 5; ++k) confident = confident && d.kps_conf[k] >= kMinLandmarkConf;
      if (confident) {
        for (int k = 0; k < 10; ++k) det.landmarks[k] = d.kps[k];
        det.has_landmarks = true;
        det.aligned = alignFace(image, det.landmarks);
      }
    }
    results.push_back(det);
  }

  std::sort(results.begin(), results.end(), std::greater<Detection>());
  return results;
}

std::vector<float> FaceDetection::preprocess(const ImageRGB& input_image) {
  return imageToChw(input_image);
}

}  // namespace biopass
