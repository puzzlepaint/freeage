#include "FreeAge/sprite_atlas.h"

#include "FreeAge/logging.h"
#include "FreeAge/sprite.h"
#include "FreeAge/timing.h"
#include "RectangleBinPack/MaxRectsBinPack.h"

using namespace rbp;

SpriteAtlas::SpriteAtlas(Mode mode)
    : mode(mode) {}

void SpriteAtlas::AddSprite(Sprite* sprite) {
  sprites.push_back(sprite);
}

bool SpriteAtlas::BuildAtlas(int width, int height, QImage* atlasImage, int borderPixels) {
  Timer packTimer("SpriteAtlas::BuildAtlas packing");
  
  // TODO: Should we allow flipping? It is currently not implemented for texture coordinate setting in rendering.
  MaxRectsBinPack packer(width, height, /*allowFlip*/ false);
  
  // TODO: Right now, we only pack the sprites' main graphics.
  //       We should pack the shadows too (into another texture, since they are only 8 bit per pixel).
  
  int numRects = 0;
  for (Sprite* sprite : sprites) {
    numRects += sprite->NumFrames();
  }
  
  std::vector<RectSize> rects(numRects);
  int index = 0;
  for (Sprite* sprite : sprites) {
    for (int frameIdx = 0; frameIdx < sprite->NumFrames(); ++ frameIdx) {
      const QImage& image =
          (mode == Mode::Graphic) ?
          sprite->frame(frameIdx).graphic.image :
          sprite->frame(frameIdx).shadow.image;
      rects[index] = RectSize{image.width() + 2 * borderPixels, image.height() + 2 * borderPixels};
      ++ index;
    }
  }
  
  std::vector<Rect> packedRects;
  std::vector<int> packedRectIndices;
  // TODO: Choose the packing rule that is best.
  //       RectBottomLeftRule is much faster than RectBestShortSideFit, so it is more convenient for testing, unless we cache the results.
  packer.Insert(rects, packedRects, packedRectIndices, MaxRectsBinPack::RectBottomLeftRule);  // RectBestShortSideFit
  packTimer.Stop();
  if (!rects.empty()) {
    // Not all rects could be added because they did not fit into the specified area.
    return false;
  }
  
  // If no output image is given, return true to signal that the images fit into the given atlas size.
  if (!atlasImage) {
    return true;
  }
  
  Timer paintTimer("SpriteAtlas::BuildAtlas rendering");
  
  // Invert packedRectIndices
  std::vector<int> originalToPackedIndex(numRects);
  for (std::size_t i = 0; i < packedRectIndices.size(); ++ i) {
    originalToPackedIndex[packedRectIndices[i]] = i;
  }
  
  // Draw all images into their assigned rects.
  QImage atlas(width, height, (mode == Mode::Graphic) ? QImage::Format_ARGB32 : QImage::Format_Grayscale8);
  // Clear the atlas to have clean borders around the sprites
  atlas.fill(qRgba(0, 0, 0, 0));  // this sets the values to 0 for QImage::Format_Grayscale8.
  
  index = 0;
  for (Sprite* sprite : sprites) {
    for (int frameIdx = 0; frameIdx < sprite->NumFrames(); ++ frameIdx) {
      Sprite::Frame::Layer& layer =
          (mode == Mode::Graphic) ?
          sprite->frame(frameIdx).graphic :
          sprite->frame(frameIdx).shadow;
      QImage& image = layer.image;
      const Rect& packedRect = packedRects[originalToPackedIndex[index]];
      
      layer.atlasX = packedRect.x + borderPixels;
      layer.atlasY = packedRect.y + borderPixels;
      
      int packedWidthWithoutBorder = packedRect.width - 2 * borderPixels;
      int packedHeightWithoutBorder = packedRect.height - 2 * borderPixels;
      
      if (packedWidthWithoutBorder == image.width() && packedHeightWithoutBorder == image.height()) {
        layer.rotated = false;
        
        // Draw the image directly into the assigned rect.
        if (mode == Mode::Graphic) {
          for (int y = 0; y < image.height(); ++ y) {
            const QRgb* inputScanline = reinterpret_cast<const QRgb*>(image.scanLine(y));
            QRgb* outputScanline = reinterpret_cast<QRgb*>(atlas.scanLine(layer.atlasY + y));
            for (int x = 0; x < image.width(); ++ x) {
              outputScanline[layer.atlasX + x] = inputScanline[x];
            }
          }
        } else {
          for (int y = 0; y < image.height(); ++ y) {
            const u8* inputScanline = reinterpret_cast<const u8*>(image.scanLine(y));
            u8* outputScanline = reinterpret_cast<u8*>(atlas.scanLine(layer.atlasY + y));
            for (int x = 0; x < image.width(); ++ x) {
              outputScanline[layer.atlasX + x] = inputScanline[x];
            }
          }
        }
      } else if (packedWidthWithoutBorder == image.height() && packedHeightWithoutBorder == image.width()) {
        layer.rotated = true;
        
        // Draw the image into the assigned rect while rotating it by 90 degrees (to the right).
        for (int y = 0; y < image.height(); ++ y) {
          const QRgb* inputScanline = reinterpret_cast<const QRgb*>(image.scanLine(y));
          for (int x = 0; x < image.width(); ++ x) {
            // TODO: Speed this up with raw QRgb or u8 access
            atlas.setPixelColor(layer.atlasX + image.height() - y, layer.atlasY + x, inputScanline[x]);
          }
        }
      } else {
        // Something went wrong.
        LOG(ERROR) << "Internal error of SpriteAtlas::BuildAtlas(): The size of the rect assigned to a sprite frame is incorrect.";
        return false;
      }
      
      // Unload the sprite image since it should not be needed anymore.
      image = QImage();
      
      // Dilate the sprite colors by one (without touching the alpha channel).
      // This prevents us from getting an ugly influence of the black (color value: (0, 0, 0, 0)) background
      // when using bilinear filtering to access pixels on the border of the sprite.
      // TODO: Use integral images for better performance
      if (mode == Mode::Graphic) {
        for (int y = 0; y < packedRect.height; ++ y) {
          QRgb* outputScanline = reinterpret_cast<QRgb*>(atlas.scanLine(packedRect.y + y));
          
          for (int x = 0; x < packedRect.width; ++ x) {
            if (qAlpha(outputScanline[packedRect.x + x]) > 0) {
              continue;
            }
            
            int minX = std::max(0, x - 1);
            int minY = std::max(0, y - 1);
            int maxX = std::min(packedRect.width - 1, x + 1);
            int maxY = std::min(packedRect.height - 1, y + 1);
            
            int redSum = 0;
            int greenSum = 0;
            int blueSum = 0;
            int count = 0;
            for (int sy = minY; sy <= maxY; ++ sy) {
              for (int sx = minX; sx <= maxX; ++ sx) {
                QRgb rgba = *(reinterpret_cast<QRgb*>(atlas.scanLine(packedRect.y + sy)) + (packedRect.x + sx));
                if (qAlpha(rgba) == 0) {
                  continue;
                }
                
                redSum += qRed(rgba);
                greenSum += qGreen(rgba);
                blueSum += qBlue(rgba);
                ++ count;
              }
            }
            
            if (count > 0) {
              float factor = 1.f / count;
              outputScanline[packedRect.x + x] =
                  qRgba(redSum * factor + 0.5f, greenSum * factor + 0.5f, blueSum * factor + 0.5f, 0);
            }
          }
        }
      }
      
      ++ index;
    }
  }
  
  *atlasImage = atlas;
  return true;
}
