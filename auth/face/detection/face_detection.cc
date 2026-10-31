#include "face_detection.h"

#include <algorithm>

#include "utils.h"

namespace biopass {

FaceDetection::FaceDetection(const ModelBytes& model, int imgsz, const float conf, const float iou)
    : conf(conf), iou(iou), imgsz(imgsz), session(model, "FaceDetection") {}

std::vector<Detection> FaceDetection::inference(const ImageRGB& image) {
  if (image.empty()) {
    return {};
  }
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
    const std::optional<PixelBox> box = clampBoxToImage(d, image.width, image.height);
    if (!box) {
      continue;
    }

    Box xyxy_box(box->x1, box->y1, box->x2, box->y2);
    ImageRGB crop_face = image.crop(box->x1, box->y1, box->x2, box->y2);
    Detection det(d.cls, std::string("face"), d.conf, xyxy_box, crop_face);
    results.push_back(det);
  }

  std::sort(results.begin(), results.end(), std::greater<Detection>());
  return results;
}

std::vector<float> FaceDetection::preprocess(const ImageRGB& input_image) {
  return imageToChw(input_image);
}

}  // namespace biopass
