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
  // TODO: It is cumbersome to choose the width and height here, as it
  //       is unknown beforehand whether the sprites will fit. Can we
  //       choose that automatically?
  QImage BuildAtlas(int width, int height);
  
 private:
  std::vector<Sprite*> sprites;
};
