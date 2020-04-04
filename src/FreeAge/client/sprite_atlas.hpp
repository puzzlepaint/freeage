#pragma once

#include <QImage>

#include <vector>

namespace rbp {
  struct Rect;
}
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
  
  ~SpriteAtlas();
  
  void AddSprite(Sprite* sprite);
  
  /// Attempts to pack all added sprites into a texture of the given size,
  /// while leaving @p borderPixels of free border around each sprite.
  /// If the SpriteAtlas fails to pack the sprites into this area, returns
  /// false.
  bool BuildAtlas(int width, int height, int borderPixels = 1);
  
  /// Saves the information computed by BuildAtlas() to the given file.
  /// Returns true on success, false otherwise.
  bool Save(const char* path);
  
  /// Loads the information computed by BuildAtlas() from the given file.
  /// Returns true on success, false otherwise.
  bool Load(const char* path, int expectedNumRects);
  
  /// May be used to check whether a loaded atlas contains the correct sprite
  /// frame dimensions for the added sprites.
  bool IsConsistent();
  
  /// May be called after BuildAtlas() succeeded to render the atlas image.
  /// Writes the atlas positions of each layer into the Sprites, and
  /// unloads the QImages in the sprite layers that were used to create the atlas.
  QImage RenderAtlas();
  
 private:
  int atlasWidth;
  int atlasHeight;
  int atlasBorderPixels;
  std::vector<rbp::Rect> packedRects;
  std::vector<int> packedRectIndices;
  
  std::vector<Sprite*> sprites;
  Mode mode;
};
