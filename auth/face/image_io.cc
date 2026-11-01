#include "image_io.h"

#include <cctype>
#include <limits>
#include <optional>

#include "stb_image.h"
#include "stb_image_write.h"
#include "user_file.h"

namespace {

std::string lowercasePathExtension(const std::string& path) {
  size_t dot = path.rfind('.');
  if (dot == std::string::npos)
    return "";
  std::string ext = path.substr(dot);
  for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
  return ext;
}

void appendToBuffer(void* context, void* data, int size) {
  auto* buf = static_cast<std::vector<uint8_t>*>(context);
  buf->insert(buf->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

}  // namespace

ImageRGB decodeImage(const std::string& bytes) {
  if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    return {};
  int w = 0, h = 0, channels = 0;
  uint8_t* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                          static_cast<int>(bytes.size()), &w, &h, &channels, 3);
  if (!pixels)
    return {};

  ImageRGB img(w, h, pixels);
  stbi_image_free(pixels);
  return img;
}

ImageRGB readImage(const std::string& path) {
  const std::optional<std::string> bytes = biopass::readRegularFile(path, kMaxImageFileBytes);
  return bytes ? decodeImage(*bytes) : ImageRGB{};
}

bool saveImage(const std::string& path, const ImageRGB& img) {
  if (img.empty())
    return false;

  std::string ext = lowercasePathExtension(path);
  int stride = img.width * 3;

  if (ext == ".jpg" || ext == ".jpeg") {
    return stbi_write_jpg(path.c_str(), img.width, img.height, 3, img.ptr(), 95) != 0;
  } else if (ext == ".png") {
    return stbi_write_png(path.c_str(), img.width, img.height, 3, img.ptr(), stride) != 0;
  } else if (ext == ".bmp") {
    return stbi_write_bmp(path.c_str(), img.width, img.height, 3, img.ptr()) != 0;
  } else if (ext == ".tga") {
    return stbi_write_tga(path.c_str(), img.width, img.height, 3, img.ptr()) != 0;
  }

  return stbi_write_png(path.c_str(), img.width, img.height, 3, img.ptr(), stride) != 0;
}

bool encodeJpeg(const ImageRGB& img, int quality, std::vector<uint8_t>& out) {
  out.clear();
  return stbi_write_jpg_to_func(&appendToBuffer, &out, img.width, img.height, 3, img.ptr(),
                                quality) != 0;
}
