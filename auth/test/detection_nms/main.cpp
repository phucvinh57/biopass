// How the YOLOv8-face detector output is turned into boxes
// (face/detection/utils.h non_max_suppression): the confidence cut, xywh ->
// xyxy, and greedy IoU suppression by confidence. Pure box maths, no model
// needed. Exits non-zero on any failure.

#include <cstdio>
#include <vector>

#include "utils.h"

namespace {

using biopass::non_max_suppression;
using biopass::RawDet;

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

struct Pred {
  float cx, cy, w, h, score;
};

// Model output layout: [pred_dim][num_preds], rows cx, cy, w, h, score and
// 15 keypoint rows (ignored, filled with junk).
constexpr int kPredDim = 20;

std::vector<float> output(const std::vector<Pred>& preds) {
  const int n = static_cast<int>(preds.size());
  std::vector<float> out(static_cast<size_t>(kPredDim) * n, -7.0f);
  for (int i = 0; i < n; ++i) {
    out[0 * n + i] = preds[i].cx;
    out[1 * n + i] = preds[i].cy;
    out[2 * n + i] = preds[i].w;
    out[3 * n + i] = preds[i].h;
    out[4 * n + i] = preds[i].score;
  }
  return out;
}

std::vector<RawDet> run(const std::vector<Pred>& preds, float conf = 0.25f, float iou = 0.45f,
                        int max_det = 300) {
  const auto out = output(preds);
  return non_max_suppression(out.data(), static_cast<int>(preds.size()), kPredDim, conf, iou,
                             max_det);
}

// A box given as corners, as a prediction with `score`.
Pred box(float x1, float y1, float x2, float y2, float score) {
  return {(x1 + x2) / 2, (y1 + y2) / 2, x2 - x1, y2 - y1, score};
}

bool same(const RawDet& d, float x1, float y1, float x2, float y2, float conf) {
  return d.x1 == x1 && d.y1 == y1 && d.x2 == x2 && d.y2 == y2 && d.conf == conf && d.cls == 0;
}

void checkConversionAndCut() {
  expect(run({}).empty(), "no predictions: no boxes");

  auto dets = run({{50, 40, 20, 10, 0.9f}});
  expect(dets.size() == 1 && same(dets[0], 40, 35, 60, 45, 0.9f),
         "cx,cy,w,h becomes x1,y1,x2,y2 with the score as conf, class 0");

  dets = run({{10, 10, 4, 4, 0.24f}, {50, 50, 4, 4, 0.25f}, {90, 90, 4, 4, 0.0f}});
  expect(dets.size() == 1 && dets[0].conf == 0.25f,
         "scores below conf_thres are dropped; a score equal to it is kept");

  dets = run({{10, 10, 4, 4, 0.5f}}, 0.6f);
  expect(dets.empty(), "conf_thres is the caller's, not a fixed 0.25");

  // The score is a sigmoid in [0, 1]; anything lower is read as 0.
  dets = run({{10, 10, 4, 4, -1.0f}}, 0.0f);
  expect(dets.size() == 1 && dets[0].conf == 0.0f,
         "a negative score counts as 0, so conf_thres 0 keeps it with conf 0");
}

void checkSuppression() {
  // IoU of A and B = 70 / 130 > 0.45: B goes. C overlaps B as much but only
  // a kept box suppresses, and C vs A = 40 / 160.
  auto dets = run({box(3, 0, 13, 10, 0.8f), box(0, 0, 10, 10, 0.9f), box(6, 0, 16, 10, 0.7f)});
  expect(dets.size() == 2 && same(dets[0], 0, 0, 10, 10, 0.9f) && same(dets[1], 6, 0, 16, 10, 0.7f),
         "highest conf first; overlaps above iou_thres dropped; dropped boxes suppress nothing");

  // B = top half of A: IoU exactly 0.5.
  dets = run({box(0, 0, 10, 10, 0.9f), box(0, 0, 10, 5, 0.8f)}, 0.25f, 0.5f);
  expect(dets.size() == 2, "IoU equal to iou_thres is kept (strictly greater suppresses)");
  dets = run({box(0, 0, 10, 10, 0.9f), box(0, 0, 10, 5, 0.8f)}, 0.25f, 0.49f);
  expect(dets.size() == 1 && dets[0].conf == 0.9f, "IoU above iou_thres is suppressed");

  dets = run({box(0, 0, 10, 10, 0.6f), box(20, 20, 30, 30, 0.9f), box(10, 0, 20, 10, 0.7f)});
  expect(dets.size() == 3 && dets[0].conf == 0.9f && dets[1].conf == 0.7f && dets[2].conf == 0.6f,
         "disjoint and edge-touching boxes are all kept, sorted by conf");

  dets = run({box(0, 0, 10, 10, 0.5f), box(1, 1, 9, 9, 0.9f)});
  expect(dets.size() == 1 && same(dets[0], 1, 1, 9, 9, 0.9f),
         "a box inside a higher-conf box: the lower-conf outer one goes (IoU 0.64)");
}

void checkMaxDet() {
  const std::vector<Pred> preds = {box(0, 0, 10, 10, 0.3f), box(20, 0, 30, 10, 0.9f),
                                   box(40, 0, 50, 10, 0.6f)};
  auto dets = run(preds, 0.25f, 0.45f, 2);
  expect(dets.size() == 2 && dets[0].conf == 0.9f && dets[1].conf == 0.6f,
         "max_det keeps the highest-conf boxes");
  expect(run(preds, 0.25f, 0.45f, 0).empty(), "max_det 0 keeps nothing");
}

}  // namespace

int main() {
  checkConversionAndCut();
  checkSuppression();
  checkMaxDet();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}
