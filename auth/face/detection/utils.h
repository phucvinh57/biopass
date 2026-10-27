#ifndef FACE_DET_UTILS_H
#define FACE_DET_UTILS_H

// CPP native
#include <vector>

namespace biopass {

struct RawDet {
  float x1, y1, x2, y2, conf;
  int cls;
  // 5 facial landmarks (x,y) in the same coordinate space as the box, plus
  // per-point confidence. has_kps is false for models without keypoints.
  float kps[10] = {0};
  float kps_conf[5] = {0};
  bool has_kps = false;
};

std::vector<RawDet> non_max_suppression(const float* output, int num_preds, int pred_dim,
                                        float conf_thres = 0.25, float iou_thres = 0.45,
                                        int max_det = 300);

void scale_boxes(const std::vector<int>& img1_shape, std::vector<RawDet>& dets,
                 const std::vector<int>& img0_shape);

}  // namespace biopass

#endif  // FACE_DET_UTILS_H
