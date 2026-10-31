#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
// Same limit as ImageRGB::kMaxDim: stb rejects a larger header before it
// allocates, so a small file can't declare a multi-gigabyte decode. It only
// takes effect here, where the decoder is compiled.
#define STBI_MAX_DIMENSIONS 16384
static_assert(STBI_MAX_DIMENSIONS == ImageRGB::kMaxDim);
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
