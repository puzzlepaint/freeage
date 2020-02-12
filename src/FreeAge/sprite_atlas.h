#pragma once

#include <QImage>

#include <vector>

class Sprite;

/// Packs one or multiple sprites into an atlas texture, where all sprite
/// frames are stored next to each other.
class SpriteAtlas {
 public:
  void AddSprite(Sprite* sprite);
  
  /// Attempts to pack all added sprites into a texture of the given size.
  /// If the SpriteAtlas fails to pack the sprites into this area, returns
  /// false. Writes the atlas positions of each layer into the Sprites.
  /// Leaves borderPixels of free border around each sprite.
  bool BuildAtlas(int width, int height, QImage* atlasImage, int borderPixels = 1);
  
 private:
  std::vector<Sprite*> sprites;
};
