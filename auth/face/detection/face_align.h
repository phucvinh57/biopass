#ifndef FACE_ALIGN_H
#define FACE_ALIGN_H

// 5-point face alignment for ArcFace-family recognizers (EdgeFace, ArcFace, ...).
// These models are trained on faces warped so that eyes/nose/mouth sit at
// canonical positions in a 112x112 frame; feeding raw bounding-box crops
// costs a large chunk of cosine similarity, especially with head tilt or
// varying distance. Header-only, no external deps.

#include <array>
#include <cmath>

#include "image_utils.h"

namespace biopass {

constexpr int kAlignedFaceSize = 112;

// Canonical ArcFace landmark positions (112x112): left eye, right eye, nose,
// left mouth corner, right mouth corner.
constexpr std::array<float, 10> kArcFaceReference = {
    38.2946f, 51.6963f, 73.5318f, 51.5014f, 56.0252f,
    71.7366f, 41.5493f, 92.3655f, 70.7299f, 92.2041f};

// Solves the least-squares 2-D similarity transform  dst = [a -b; b a] * src + [tx ty]
// mapping the 5 source landmarks onto the reference points. Returns false if
// the system is degenerate (collinear/duplicate landmarks).
inline bool estimateSimilarity(const std::array<float, 10>& src, const std::array<float, 10>& dst,
                               double out[4]) {
  // Normal equations for p = (a, b, tx, ty). Each landmark contributes two rows:
  //   [ x  -y  1  0 ] p = x'
  //   [ y   x  0  1 ] p = y'
  double AtA[4][4] = {};
  double Atb[4] = {};
  for (int i = 0; i < 5; ++i) {
    const double x = src[i * 2], y = src[i * 2 + 1];
    const double xp = dst[i * 2], yp = dst[i * 2 + 1];
    const double r0[4] = {x, -y, 1.0, 0.0};
    const double r1[4] = {y, x, 0.0, 1.0};
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        AtA[r][c] += r0[r] * r0[c] + r1[r] * r1[c];
      }
      Atb[r] += r0[r] * xp + r1[r] * yp;
    }
  }
  // Gaussian elimination with partial pivoting on the 4x4 system.
  double M[4][5];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) M[r][c] = AtA[r][c];
    M[r][4] = Atb[r];
  }
  for (int col = 0; col < 4; ++col) {
    int piv = col;
    for (int r = col + 1; r < 4; ++r) {
      if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
    }
    if (std::fabs(M[piv][col]) < 1e-9) return false;
    if (piv != col) {
      for (int c = 0; c < 5; ++c) std::swap(M[col][c], M[piv][c]);
    }
    for (int r = 0; r < 4; ++r) {
      if (r == col) continue;
      const double f = M[r][col] / M[col][col];
      for (int c = col; c < 5; ++c) M[r][c] -= f * M[col][c];
    }
  }
  for (int r = 0; r < 4; ++r) out[r] = M[r][4] / M[r][r];
  const double scale = std::hypot(out[0], out[1]);
  return std::isfinite(scale) && scale > 1e-6;
}

// Warps `src` so that `landmarks` (5 x (x,y) in src pixel coords) land on the
// ArcFace reference points. Output is kAlignedFaceSize square. Returns an
// empty image if the transform cannot be estimated.
inline ImageRGB alignFace(const ImageRGB& src, const std::array<float, 10>& landmarks) {
  double p[4];
  if (src.empty() || !estimateSimilarity(landmarks, kArcFaceReference, p)) {
    return {};
  }
  const double a = p[0], b = p[1], tx = p[2], ty = p[3];
  // Invert: src = R^-1 (dst - t), with R = s*[cos -sin; sin cos] => R^-1 = (1/s^2) [a b; -b a]
  const double s2 = a * a + b * b;
  const double ia = a / s2, ib = b / s2;

  const int N = kAlignedFaceSize;
  ImageRGB out(N, N);
  for (int y = 0; y < N; ++y) {
    for (int x = 0; x < N; ++x) {
      const double dx = x - tx, dy = y - ty;
      const double sx = ia * dx + ib * dy;
      const double sy = -ib * dx + ia * dy;
      // Bilinear sample with zero (black) outside the source.
      const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
      const double fx = sx - x0, fy = sy - y0;
      for (int c = 0; c < 3; ++c) {
        double acc = 0.0;
        const int xs[2] = {x0, x0 + 1};
        const int ys[2] = {y0, y0 + 1};
        const double wx[2] = {1.0 - fx, fx};
        const double wy[2] = {1.0 - fy, fy};
        for (int j = 0; j < 2; ++j) {
          for (int i = 0; i < 2; ++i) {
            const int xi = xs[i], yi = ys[j];
            if (xi < 0 || yi < 0 || xi >= src.width || yi >= src.height) continue;
            acc += wx[i] * wy[j] * src.at(yi, xi, c);
          }
        }
        out.at(y, x, c) = static_cast<uint8_t>(std::lround(std::min(255.0, std::max(0.0, acc))));
      }
    }
  }
  return out;
}

}  // namespace biopass

#endif  // FACE_ALIGN_H
