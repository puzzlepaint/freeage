#include "FreeAge/sprite_atlas.h"

#include "FreeAge/logging.h"
#include "FreeAge/sprite.h"
#include "FreeAge/timing.h"
#include "RectangleBinPack/MaxRectsBinPack.h"

using namespace rbp;

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
      const QImage& image = sprite->frame(frameIdx).graphic.image;
      rects[index] = RectSize{image.width() + 2 * borderPixels, image.height() + 2 * borderPixels};
      ++ index;
    }
  }
  
  std::vector<Rect> packedRects;
  std::vector<int> packedRectIndices;
  packer.Insert(rects, packedRects, packedRectIndices, MaxRectsBinPack::RectBestShortSideFit);
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
  QImage atlas(width, height, QImage::Format_ARGB32);
  // Clear the atlas to have clean borders around the sprites
  atlas.fill(qRgba(0, 0, 0, 0));
  
  index = 0;
  for (Sprite* sprite : sprites) {
    for (int frameIdx = 0; frameIdx < sprite->NumFrames(); ++ frameIdx) {
      Sprite::Frame::Layer& layer = sprite->frame(frameIdx).graphic;
      const QImage& image = layer.image;
      const Rect& packedRect = packedRects[originalToPackedIndex[index]];
      
      layer.atlasX = packedRect.x + borderPixels;
      layer.atlasY = packedRect.y + borderPixels;
      
      int packedWidthWithoutBorder = packedRect.width - 2 * borderPixels;
      int packedHeightWithoutBorder = packedRect.height - 2 * borderPixels;
      
      if (packedWidthWithoutBorder == image.width() && packedHeightWithoutBorder == image.height()) {
        layer.rotated = false;
        
        // Draw the image directly into the assigned rect.
        for (int y = 0; y < image.height(); ++ y) {
          for (int x = 0; x < image.width(); ++ x) {
            // TODO: Speed this up with raw access
            atlas.setPixelColor(layer.atlasX + x, layer.atlasY + y, image.pixelColor(x, y));
          }
        }
      } else if (packedWidthWithoutBorder == image.height() && packedHeightWithoutBorder == image.width()) {
        layer.rotated = true;
        
        // Draw the image into the assigned rect while rotating it by 90 degrees (to the right).
        for (int y = 0; y < image.height(); ++ y) {
          for (int x = 0; x < image.width(); ++ x) {
            // TODO: Speed this up with raw access
            atlas.setPixelColor(layer.atlasX + image.height() - y, layer.atlasY + x, image.pixelColor(x, y));
          }
        }
      } else {
        // Something went wrong.
        LOG(ERROR) << "Internal error of SpriteAtlas::BuildAtlas(): The size of the rect assigned to a sprite frame is incorrect.";
        return false;
      }
      
      ++ index;
    }
  }
  
  *atlasImage = atlas;
  return true;
}
