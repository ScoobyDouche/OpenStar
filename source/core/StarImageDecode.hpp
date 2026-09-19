#pragma once

#include "StarImage.hpp"
#include "StarByteArray.hpp"
#include "StarMaybe.hpp"

namespace Star {

// Decodes PNG, JPEG, GIF (first frame) or BMP bytes into an RGBA32 image with
// rows stored bottom-up, like Image::readPng.  Returns nothing on failure.
Maybe<Image> decodeImage(ByteArray const& bytes);

// Nearest-neighbour downscale of an RGBA32 image so that neither side exceeds
// maxSide, preserving aspect ratio.  Smaller images are returned unchanged.
Image fitImage(Image const& image, unsigned maxSide);

}
