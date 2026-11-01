#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "image.h"

// Encoding and decoding through stb. This and stb_impl.cc are the only
// library translation units that include stb.

// Enrolled faces are small crops; this only stops absurd files.
inline constexpr size_t kMaxImageFileBytes = 32u << 20;

// Any stb_image format, detected from the encoded bytes.
ImageRGB decodeImage(const std::string& bytes);

// Reads a regular image file (see core/user_file.h). In the root helper,
// read user-controlled paths with readUserFile() + decodeImage() instead.
ImageRGB readImage(const std::string& path);

// Format by extension: .jpg/.jpeg (quality 95), .bmp, .tga, otherwise PNG.
bool saveImage(const std::string& path, const ImageRGB& img);

// JPEG at `quality` (1-100) into `out`, which is cleared first.
bool encodeJpeg(const ImageRGB& img, int quality, std::vector<uint8_t>& out);
