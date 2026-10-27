#include "utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace biopass {

namespace {

std::vector<int> nms(const std::vector<RawDet>& dets, float iou_threshold) {
  if (dets.empty())
    return {};

  std::vector<int> indices(dets.size());
  std::iota(indices.begin(), indices.end(), 0);
  std::sort(indices.begin(), indices.end(),
            [&](int a, int b) { return dets[a].conf > dets[b].conf; });

  std::vector<bool> suppressed(dets.size(), false);
  std::vector<int> keep;

  for (int idx : indices) {
    if (suppressed[idx])
      continue;
    keep.push_back(idx);

    auto ix1 = dets[idx].x1, iy1 = dets[idx].y1;
    auto ix2 = dets[idx].x2, iy2 = dets[idx].y2;
    auto iarea = (ix2 - ix1) * (iy2 - iy1);

    for (size_t j = 0; j < indices.size(); j++) {
      int jdx = indices[j];
      if (suppressed[jdx] || jdx == idx)
        continue;

      auto xx1 = std::max(ix1, dets[jdx].x1);
      auto yy1 = std::max(iy1, dets[jdx].y1);
      auto xx2 = std::min(ix2, dets[jdx].x2);
      auto yy2 = std::min(iy2, dets[jdx].y2);

      auto w = std::max(0.0f, xx2 - xx1);
      auto h = std::max(0.0f, yy2 - yy1);
      auto inter = w * h;
      auto jarea = (dets[jdx].x2 - dets[jdx].x1) * (dets[jdx].y2 - dets[jdx].y1);
      auto ovr = inter / (iarea + jarea - inter);
      if (ovr > iou_threshold)
        suppressed[jdx] = true;
    }
  }
  return keep;
}

}  // namespace

std::vector<RawDet> non_max_suppression(const float* output, int num_preds, int pred_dim,
                                        float conf_thres, float iou_thres, int max_det) {
  // YOLOv8-face output shape: [1, pred_dim, num_preds]
  // pred_dim=20: 4 (xywh) + 1 (face class) + 15 (5 keypoints * 3)
  // We use index 4 as the face class score (nc=1 for face-only models)
  int nc = 1;

  std::vector<RawDet> candidates;
  for (int i = 0; i < num_preds; i++) {
    float max_score = 0;
    int max_cls = 0;
    for (int c = 0; c < nc; c++) {
      float score = output[(4 + c) * num_preds + i];
      if (score > max_score) {
        max_score = score;
        max_cls = c;
      }
    }
    if (max_score < conf_thres)
      continue;

    float cx = output[0 * num_preds + i];
    float cy = output[1 * num_preds + i];
    float w = output[2 * num_preds + i];
    float h = output[3 * num_preds + i];

    RawDet d;
    d.x1 = cx - w / 2.0f;
    d.y1 = cy - h / 2.0f;
    d.x2 = cx + w / 2.0f;
    d.y2 = cy + h / 2.0f;
    d.conf = max_score;
    d.cls = max_cls;
    // Keypoints follow the box + class scores: 5 x (x, y, conf).
    if (pred_dim >= 4 + nc + 15) {
      d.has_kps = true;
      for (int k = 0; k < 5; ++k) {
        const int base = 4 + nc + k * 3;
        d.kps[k * 2] = output[(base + 0) * num_preds + i];
        d.kps[k * 2 + 1] = output[(base + 1) * num_preds + i];
        d.kps_conf[k] = output[(base + 2) * num_preds + i];
      }
    }
    candidates.push_back(d);
  }

  auto keep_indices = nms(candidates, iou_thres);
  std::vector<RawDet> result;
  for (int i = 0; i < std::min(static_cast<int>(keep_indices.size()), max_det); i++) {
    result.push_back(candidates[keep_indices[i]]);
  }
  return result;
}

void scale_boxes(const std::vector<int>& img1_shape, std::vector<RawDet>& dets,
                 const std::vector<int>& img0_shape) {
  auto gain =
      (std::min)((float)img1_shape[0] / img0_shape[0], (float)img1_shape[1] / img0_shape[1]);
  auto pad0 = std::round((float)(img1_shape[1] - img0_shape[1] * gain) / 2. - 0.1);
  auto pad1 = std::round((float)(img1_shape[0] - img0_shape[0] * gain) / 2. - 0.1);

  for (auto& d : dets) {
    d.x1 = (d.x1 - pad0) / gain;
    d.y1 = (d.y1 - pad1) / gain;
    d.x2 = (d.x2 - pad0) / gain;
    d.y2 = (d.y2 - pad1) / gain;
    if (d.has_kps) {
      for (int k = 0; k < 5; ++k) {
        d.kps[k * 2] = (d.kps[k * 2] - pad0) / gain;
        d.kps[k * 2 + 1] = (d.kps[k * 2 + 1] - pad1) / gain;
      }
    }
  }
}

}  // namespace biopass
