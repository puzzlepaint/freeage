// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QImage>

#include "FreeAge/common/free_age.hpp"

/// Creates a map with the same size as the given image, where each pixel
/// is 1 if the alpha value of the original pixel is larger than 127 and
/// 0 otherwise. This can be used to create maps for UI graphics which
/// can be used to decide whether the UI element was clicked or not.
/// This function assumes that the image is given in format QImage::Format_ARGB32.
/// The map is stored with only 1 bit per pixel to save memory.
class OpaquenessMap {
 public:
  OpaquenessMap() = default;
  
  OpaquenessMap(const QImage& image);
  
  void Create(const QImage& image);
  
  inline bool IsOpaque(int x, int y) {
    if (x < 0 || y < 0 ||
        x >= map.width() || y >= map.height()) {
      return false;
    }
    
    u8* scanLine = map.scanLine(y);
    return scanLine[x >> 3] & (0x80 >> (x & 7));
  }
  
 private:
  QImage map;
};
