#include "StarImageDecode.hpp"

#include "gtest/gtest.h"

using namespace Star;

// 2x2 24-bit BMP. Top row: red, green. Bottom row: blue, white.
static ByteArray makeTestBmp() {
  unsigned char const bytes[] = {
    // BITMAPFILEHEADER: "BM", file size 70, reserved, pixel offset 54
    'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
    // BITMAPINFOHEADER: size 40, width 2, height 2, planes 1, 24 bpp, no compression, image size 16
    40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
    0, 0, 0, 0, 16, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    // Pixel rows are bottom-up, BGR, padded to 4 bytes
    0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0, 0,  // bottom: blue, white
    0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0, 0   // top: red, green
  };
  return ByteArray((char const*)bytes, sizeof(bytes));
}

TEST(ImageDecodeTest, DecodesBmpBottomUp) {
  auto image = decodeImage(makeTestBmp());
  ASSERT_TRUE(image.isValid());
  EXPECT_EQ(image->width(), 2u);
  EXPECT_EQ(image->height(), 2u);
  EXPECT_EQ(image->pixelFormat(), PixelFormat::RGBA32);
  EXPECT_EQ(image->get(0, 0), Vec4B(0, 0, 255, 255));
  EXPECT_EQ(image->get(1, 0), Vec4B(255, 255, 255, 255));
  EXPECT_EQ(image->get(0, 1), Vec4B(255, 0, 0, 255));
  EXPECT_EQ(image->get(1, 1), Vec4B(0, 255, 0, 255));
}

TEST(ImageDecodeTest, RejectsGarbage) {
  EXPECT_FALSE(decodeImage(ByteArray("not an image", 12)).isValid());
  EXPECT_FALSE(decodeImage(ByteArray()).isValid());
}

TEST(ImageDecodeTest, FitImageShrinksLargeImages) {
  Image large = Image::filled(Vec2U(1000, 500), Vec4B(10, 20, 30, 255));
  Image fitted = fitImage(large, 512);
  EXPECT_EQ(fitted.width(), 512u);
  EXPECT_EQ(fitted.height(), 256u);
  EXPECT_EQ(fitted.get(100, 100), Vec4B(10, 20, 30, 255));
}

TEST(ImageDecodeTest, FitImageKeepsSmallImages) {
  Image small = Image::filled(Vec2U(100, 50), Vec4B(1, 2, 3, 4));
  Image fitted = fitImage(small, 512);
  EXPECT_EQ(fitted.width(), 100u);
  EXPECT_EQ(fitted.height(), 50u);
}
