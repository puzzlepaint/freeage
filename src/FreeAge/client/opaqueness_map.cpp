#include "FreeAge/client/opaqueness_map.hpp"

#include "FreeAge/common/logging.hpp"

OpaquenessMap::OpaquenessMap(const QImage& image) {
  Create(image);
}

void OpaquenessMap::Create(const QImage& image) {
  if (image.format() != QImage::Format_ARGB32) {
    LOG(ERROR) << "The given image format is not supported: " << image.format();
    return;
  }
  
  map = QImage(image.width(), image.height(), QImage::Format_Mono);
  for (int y = 0; y < image.height(); ++ y) {
    const QRgb* in = reinterpret_cast<const QRgb*>(image.scanLine(y));
    u8* out = map.scanLine(y);
    
    for (int x = 0; x < image.width(); ++ x) {
      // 0x80 has only the leftmost bit set within 8 bits.
      // This bit is shifted right for each increase in x,
      // wrapping around at 8.
      if (qAlpha(*in) >= 128) {
        (*out) |= (0x80 >> (x & 7));
      } else {
        (*out) &= ~(0x80 >> (x & 7));
      }
      
      ++ in;
      if (x % 8 == 7) {
        ++ out;
      }
    }
    
    CHECK_EQ(in, reinterpret_cast<const QRgb*>(image.scanLine(y)) + image.width());
    CHECK_EQ(out, map.scanLine(y) + (image.width() / 8));
  }
}
