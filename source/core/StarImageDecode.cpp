#include "StarImageDecode.hpp"

#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Star {

Maybe<Image> decodeImage(ByteArray const& bytes) {
  if (bytes.empty())
    return {};

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory((stbi_uc const*)bytes.ptr(), (int)bytes.size(), &width, &height, &channels, 4);
  if (!pixels)
    return {};

  Image image((unsigned)width, (unsigned)height, PixelFormat::RGBA32);
  size_t stride = (size_t)width * 4;
  // stb_image returns rows top-down, Star::Image stores them bottom-up.
  for (int row = 0; row < height; ++row)
    std::memcpy(image.data() + (size_t)(height - row - 1) * stride, pixels + (size_t)row * stride, stride);

  stbi_image_free(pixels);
  return image;
}

Image fitImage(Image const& image, unsigned maxSide) {
  unsigned width = image.width();
  unsigned height = image.height();
  if (width <= maxSide && height <= maxSide)
    return image;

  double scale = (double)maxSide / (double)std::max(width, height);
  unsigned newWidth = std::max(1u, (unsigned)(width * scale));
  unsigned newHeight = std::max(1u, (unsigned)(height * scale));

  Image result(newWidth, newHeight, PixelFormat::RGBA32);
  for (unsigned y = 0; y < newHeight; ++y) {
    unsigned sourceY = std::min(height - 1, (unsigned)(y / scale));
    for (unsigned x = 0; x < newWidth; ++x) {
      unsigned sourceX = std::min(width - 1, (unsigned)(x / scale));
      result.set(x, y, image.get(sourceX, sourceY));
    }
  }
  return result;
}

}
