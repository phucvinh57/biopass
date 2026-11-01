#ifndef FACE_DET_UTILS_H
#define FACE_DET_UTILS_H

#include <optional>
#include <vector>

namespace biopass {

struct RawDet {
  float x1, y1, x2, y2, conf;
  int cls;
};

std::vector<RawDet> non_max_suppression(const float* output, int num_preds, int pred_dim,
                                        float conf_thres = 0.25, float iou_thres = 0.45,
                                        int max_det = 300);

void scale_boxes(const std::vector<int>& img1_shape, std::vector<RawDet>& dets,
                 const std::vector<int>& img0_shape);

struct PixelBox {
  int x1, y1, x2, y2;
};

// Clamps a box in image coordinates to [0, width] x [0, height] before the
// int cast (truncating, as before). nullopt for non-finite coordinates or a
// box with no whole pixel inside the image.
std::optional<PixelBox> clampBoxToImage(const RawDet& det, int width, int height);

}  // namespace biopass

#endif  // FACE_DET_UTILS_H
