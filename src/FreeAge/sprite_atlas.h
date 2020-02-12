#pragma once

#include <QImage>

#include <vector>

class Sprite;

/// Packs one or multiple sprites into an atlas texture, where all sprite
/// frames are stored next to each other.
class SpriteAtlas {
 public:
  enum class Mode {
    Graphic = 0,
    Shadow
  };
  
  SpriteAtlas(Mode mode);
  
  void AddSprite(Sprite* sprite);
  
  /// Attempts to pack all added sprites into a texture of the given size,
  /// while leaving @p borderPixels of free border around each sprite.
  /// If the SpriteAtlas fails to pack the sprites into this area, returns
  /// false.
  ///
  /// If atlasImage is nullptr, only tests whether the sprites fit into the
  /// texture. Otherwise, the atlas is created. In this case, the function
  /// writes the atlas positions of each layer into the Sprites, and
  /// unloads the QImages in the sprite layers that were used to create the atlas.
  bool BuildAtlas(int width, int height, QImage* atlasImage, int borderPixels = 1);
  
 private:
  std::vector<Sprite*> sprites;
  Mode mode;
};
