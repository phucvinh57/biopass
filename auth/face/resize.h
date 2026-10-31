#pragma once

#include <cstdint>

#include "image.h"

// Resampling, letterboxing and reflect-padded crops. Every function returns
// an empty image for an empty source or an unusable target size.

// Bilinear.
ImageRGB resizeImage(const ImageRGB& src, int tw, int th);

// Area-average, for downscaling.
ImageRGB resizeImageArea(const ImageRGB& src, int tw, int th);

// Lanczos4 resize (upscaling): separable 8-tap windowed-sinc resampling.
ImageRGB resizeImageLanczos4(const ImageRGB& src, int tw, int th);

// Fit inside tw x th keeping aspect ratio (bilinear, scaled size rounded),
// centred and padded with pad_val. Detection input: matches the YOLO
// letterbox that scale_boxes() undoes.
ImageRGB imageLetterbox(const ImageRGB& src, int tw, int th, uint8_t pad_val = 114);

// Aspect-preserving resize with zero padding (recognition input).
ImageRGB imageResizePad(const ImageRGB& src, int tw, int th);

// Mirrors an out-of-range index into [0, n) without repeating the edge pixel
// (cv::BORDER_REFLECT_101).
int reflect101Index(int i, int n);

// Letterbox to imgsz x imgsz (Lanczos4 up, Area down, scaled size
// truncated), reflect-101 padding. MiniFASv2 input; deliberately not
// imageLetterbox(): it mirrors the model's own preprocessing, and the two
// differ in rounding, resampling and padding.
ImageRGB imageLetterboxReflect101(const ImageRGB& src, int imgsz);

// Square crop of side `scale * max(w, h)` centred on the box, reflect-101
// outside the image. Mirrors crop() in suriAI/face-antispoof-onnx
// src/inference/preprocess.py.
ImageRGB imageSquareCropReflect101(const ImageRGB& src, int x1, int y1, int x2, int y2,
                                   float scale);
