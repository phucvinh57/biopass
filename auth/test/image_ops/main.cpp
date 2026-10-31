// Pins the outputs of the face image helpers (resize, letterbox, crop,
// CHW tensors) so moving or deduplicating them can't change what the models
// see. Hashes and boxes were recorded from the implementation at the time
// the test was added. Exits non-zero on any failure.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "image_utils.h"
#include "utils.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

// Deterministic non-uniform test pattern.
ImageRGB pattern(int w, int h) {
  ImageRGB img(w, h);
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      img.at(y, x, 0) = static_cast<uint8_t>(x * 7 + y * 3);
      img.at(y, x, 1) = static_cast<uint8_t>(x * 13 + y * 5 + 40);
      img.at(y, x, 2) = static_cast<uint8_t>((x ^ y) * 11);
    }
  }
  return img;
}

ImageRGB solid(int w, int h, uint8_t v) {
  ImageRGB img(w, h);
  for (auto& b : img.data) b = v;
  return img;
}

// FNV-1a over dims and pixels.
uint64_t hashImage(const ImageRGB& img) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint8_t b) {
    h ^= b;
    h *= 1099511628211ull;
  };
  for (int v : {img.width, img.height}) {
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>(v >> (8 * i)));
  }
  for (uint8_t b : img.data) mix(b);
  return h;
}

void checkHash(const char* what, const ImageRGB& img, uint64_t want) {
  const uint64_t h = hashImage(img);
  if (h != want) {
    std::printf("  %s = 0x%016llxull (%dx%d)\n", what, static_cast<unsigned long long>(h),
                img.width, img.height);
  }
  expect(h == want, what);
}

struct Rect {
  int x1, y1, x2, y2;
};

// Bounding box of the pixels that differ from `pad` (end-exclusive).
Rect contentRect(const ImageRGB& img, uint8_t pad) {
  Rect r{img.width, img.height, 0, 0};
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      if (img.at(y, x, 0) != pad) {
        r.x1 = std::min(r.x1, x);
        r.y1 = std::min(r.y1, y);
        r.x2 = std::max(r.x2, x + 1);
        r.y2 = std::max(r.y2, y + 1);
      }
    }
  }
  return r;
}

void checkResize() {
  const ImageRGB src = pattern(64, 48);
  const ImageRGB face = pattern(30, 41);
  checkHash("resizeImage 64x48 -> 37x29", resizeImage(src, 37, 29), 0xc49a0a47b60a2ef5ull);
  checkHash("resizeImage 30x41 -> 80x80 (up)", resizeImage(face, 80, 80), 0x1ec5ba7431dd0637ull);
  checkHash("resizeImageArea 64x48 -> 21x13", resizeImageArea(src, 21, 13), 0xa0ce2ba162ff6fb4ull);
  checkHash("resizeImageArea 30x41 -> 30x41 (identity)", resizeImageArea(face, 30, 41),
            0x30712b34ac83d47dull);
  checkHash("resizeImageLanczos4 30x41 -> 67x89", resizeImageLanczos4(face, 67, 89),
            0xaf53f0349bbc1ea5ull);
  expect(resizeImageArea(face, 30, 41).data == face.data,
         "resizeImageArea to the same size is a copy");
  expect(resizeImageArea(src, 0, 5).empty() && resizeImageLanczos4(src, 5, -1).empty(),
         "area / lanczos to a non-positive size are empty");
}

// Detection letterbox (bilinear, pad 114): where the image lands for odd
// frame sizes, and scale_boxes() maps that box back onto the whole frame.
void checkDetectionLetterbox() {
  struct Case {
    int w, h;
    Rect want;
  } cases[] = {
      {641, 479, {0, 81, 640, 559}},  {1280, 721, {0, 139, 640, 500}},
      {333, 777, {183, 0, 457, 640}}, {1920, 1080, {0, 140, 640, 500}},
      {999, 1, {0, 319, 640, 320}},   {1, 999, {319, 0, 320, 640}},
  };
  for (const auto& c : cases) {
    const ImageRGB out = imageLetterbox(solid(c.w, c.h, 200), 640, 640);
    const Rect r = contentRect(out, 114);
    char what[128];
    std::snprintf(what, sizeof(what), "imageLetterbox %dx%d -> 640: content %d,%d,%d,%d", c.w, c.h,
                  r.x1, r.y1, r.x2, r.y2);
    expect(r.x1 == c.want.x1 && r.y1 == c.want.y1 && r.x2 == c.want.x2 && r.y2 == c.want.y2, what);

    std::vector<biopass::RawDet> dets{{(float)r.x1, (float)r.y1, (float)r.x2, (float)r.y2, 1.f, 0}};
    biopass::scale_boxes({640, 640}, dets, {c.h, c.w});
    const auto& d = dets[0];
    const float tol = std::max(c.w, c.h) / 640.f + 0.01f;  // one letterbox pixel
    std::snprintf(
        what, sizeof(what),
        "scale_boxes maps %dx%d letterbox content back to the frame (%.2f,%.2f,%.2f,%.2f)", c.w,
        c.h, d.x1, d.y1, d.x2, d.y2);
    expect(std::fabs(d.x1) <= tol && std::fabs(d.y1) <= tol && std::fabs(d.x2 - c.w) <= tol &&
               std::fabs(d.y2 - c.h) <= tol,
           what);
  }
  checkHash("imageLetterbox 641x479 -> 640 (pattern)", imageLetterbox(pattern(641, 479), 640, 640),
            0x6bd039e75a1452ddull);
  checkHash("imageLetterbox 37x23 -> 64x48 (non-square)", imageLetterbox(pattern(37, 23), 64, 48),
            0x96a341e6083425efull);
}

// Recognition: zero-padded letterbox to 112.
void checkResizePad() {
  struct Case {
    int w, h;
    Rect want;
  } cases[] = {
      {30, 41, {15, 0, 97, 112}}, {57, 23, {0, 33, 112, 78}}, {113, 111, {0, 1, 112, 111}}};
  for (const auto& c : cases) {
    const Rect r = contentRect(imageResizePad(solid(c.w, c.h, 200), 112, 112), 0);
    char what[128];
    std::snprintf(what, sizeof(what), "imageResizePad %dx%d -> 112: content %d,%d,%d,%d", c.w, c.h,
                  r.x1, r.y1, r.x2, r.y2);
    expect(r.x1 == c.want.x1 && r.y1 == c.want.y1 && r.x2 == c.want.x2 && r.y2 == c.want.y2, what);
  }
  checkHash("imageResizePad 57x23 -> 112 (pattern)", imageResizePad(pattern(57, 23), 112, 112),
            0x35f7056a7b1de9deull);
}

// MiniFASv2: square context crop + reflect-101 letterbox.
void checkAntispoofPreprocess() {
  checkHash("imageLetterboxReflect101 31x47 -> 128 (up)",
            imageLetterboxReflect101(pattern(31, 47), 128), 0x0292ef85230565feull);
  checkHash("imageLetterboxReflect101 129x77 -> 128 (down)",
            imageLetterboxReflect101(pattern(129, 77), 128), 0x126ef7b3627cb024ull);
  checkHash("imageLetterboxReflect101 300x199 -> 80 (down)",
            imageLetterboxReflect101(pattern(300, 199), 80), 0xe4da066c431f5166ull);
  checkHash("imageLetterboxReflect101 80x80 -> 80 (same)",
            imageLetterboxReflect101(pattern(80, 80), 80), 0xdada5e42eaf19023ull);
  // 67 * 128 / 101 = 84.9: the short side truncates to 84 here, where
  // imageLetterbox would round it to 85.
  checkHash("imageLetterboxReflect101 101x67 -> 128 (truncating short side)",
            imageLetterboxReflect101(pattern(101, 67), 128), 0x5594733a8f248447ull);
  checkHash("imageSquareCropReflect101 1.5x inside",
            imageSquareCropReflect101(pattern(120, 90), 30, 20, 71, 63, 1.5f),
            0x4d82887851840fe3ull);
  checkHash("imageSquareCropReflect101 1.5x at the corner",
            imageSquareCropReflect101(pattern(120, 90), 90, 60, 119, 89, 1.5f),
            0x306d52c88707440full);

  expect(reflect101Index(-3, 5) == 3 && reflect101Index(9, 5) == 1 && reflect101Index(-9, 5) == 1 &&
             reflect101Index(4, 5) == 4,
         "reflect101Index mirrors repeatedly");
}

void checkTensors() {
  ImageRGB img(2, 2);
  const uint8_t px[12] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 255};
  img.data.assign(px, px + 12);

  const std::vector<float> chw = imageToChw(img);
  bool ok = chw.size() == 12;
  for (int c = 0; ok && c < 3; ++c)
    for (int i = 0; i < 4; ++i) ok = ok && chw[c * 4 + i] == px[i * 3 + c] / 255.0f;
  expect(ok, "imageToChw is planar RGB / 255");
  expect(chw[2] == 60 / 255.0f && chw[11] == 1.0f, "imageToChw spot values");

  const float mean[3] = {0.5f, 0.25f, 0.f};
  const float stdv[3] = {0.5f, 2.f, 0.25f};
  const std::vector<float> norm = imageToChwNormalized(img, mean, stdv);
  ok = norm.size() == 12;
  for (int c = 0; ok && c < 3; ++c)
    for (int i = 0; i < 4; ++i)
      ok = ok && norm[c * 4 + i] == (px[i * 3 + c] / 255.0f - mean[c]) / stdv[c];
  expect(ok, "imageToChwNormalized is (v / 255 - mean) / std per plane");
  expect(norm[0] == -1.0f && norm[11] == 4.0f, "imageToChwNormalized spot values");

  expect(imageToChw(ImageRGB()).empty(), "imageToChw of empty is empty");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0x306d52c88707440full);  // keep output if a check crashes
  checkResize();
  checkDetectionLetterbox();
  checkResizePad();
  checkAntispoofPreprocess();
  checkTensors();

  std::printf("%s\n", g_failures == 0 ? "All checks passed" : "Some checks FAILED");
  return g_failures == 0 ? 0 : 1;
}
