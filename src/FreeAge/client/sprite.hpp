#pragma once

#include <filesystem>
#include <iostream>
#include <QImage>
#include <QOpenGLFunctions_3_2_Core>
#include <QRgb>
#include <unordered_map>
#include <vector>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/texture.hpp"

class SpriteShader;
class Texture;


typedef std::vector<QRgb> Palette;

typedef std::unordered_map<int, Palette> Palettes;


#pragma pack(push, 1)
struct SMPLayerRowEdge {
  u16 leftSpace;
  u16 rightSpace;
};
#pragma pack(pop)


/// A sprite consisting of one or multiple frames, for example loaded from an .smx file.
/// Each frame may have a main graphic, a shadow, and an outline.
/// There is also some additional metadata such as the sprite's center point.
class Sprite {
 public:
  struct Frame {
    struct Layer {
      QImage image;
      int imageWidth = -1;
      int imageHeight = -1;
      int centerX = -1;
      int centerY = -1;
      
      // The layer's position in the texture atlas.
      int atlasX;
      int atlasY;
      bool rotated;
    };
    
    /// Vector of size graphic.imageHeight, giving the row edges (distance from left/right
    /// to first non-transparent pixel) for the graphic layer.
    std::vector<SMPLayerRowEdge> rowEdges;
    
    Layer graphic;
    Layer shadow;
    Layer outline;
  };
  
  Sprite() = default;
  
  bool LoadFromFile(const char* path, const Palettes& palettes);
  
  inline bool HasShadow() const { return frames.front().shadow.centerX >= 0; }
  inline bool HasOutline() const { return frames.front().outline.centerX >= 0; }
  
  inline int NumFrames() const { return frames.size(); }
  inline Frame& frame(int index) { return frames[index]; }
  inline const Frame& frame(int index) const {
    #ifndef NDEBUG
      CHECK_GE(index, 0);
      CHECK_LT(index, frames.size());
    #endif
    return frames[index];
  }
  
 private:
  bool LoadFromSMXFile(FILE* file, const Palettes& palettes);
  bool LoadFromSMPFile(FILE* file, const Palettes& palettes);
  bool LoadFromPNGFiles(const char* path);
  
  std::vector<Frame> frames;
};


struct SpriteAndTextures {
  Sprite sprite;
  Texture graphicTexture;
  Texture shadowTexture;
  
  int referenceCount;
};


/// Singleton class which keeps track of loaded sprites and the textures generated from them (with reference counting)
/// in order to avoid duplicate loading of sprites and sprite textures.
class SpriteManager {
 public:
  static SpriteManager& Instance() {
    static SpriteManager instance;
    return instance;
  }
  
  /// Loads the given sprite with the given settings, or returns an existing instance if available.
  /// The returned pointer must not be freed, but must be passed to Dereference() once it is not needed anymore.
  /// The function returns nullptr if it fails to load the given file.
  SpriteAndTextures* GetOrLoad(const char* path, const char* cachePath, const Palettes& palettes);
  
  /// Must be called once the sprite is not needed anymore. Once all references are gone, the sprite is unloaded.
  void Dereference(SpriteAndTextures* sprite);
  
 private:
  SpriteManager() = default;
  ~SpriteManager();
  
  std::unordered_map<std::string, SpriteAndTextures*> loadedSprites;
};


/// Convenience function which loads a sprite and creates a texture atlas (just) for it.
/// Attempts to find a good texture size automatically.
bool LoadSpriteAndTexture(const char* path, const char* cachePath, int wrapMode, int magFilter, int minFilter, Sprite* sprite, Texture* graphicTexture, Texture* shadowTexture, const Palettes& palettes);

void DrawSprite(
    const Sprite& sprite,
    const Texture& texture,
    SpriteShader* spriteShader,
    const QPointF& centerProjectedCoord,
    GLuint pointBuffer,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    int frameNumber,
    bool shadow,
    bool outline,
    QRgb outlineColor,
    int playerIndex,
    float scaling = 1);


// Note: SMX / SMP parsing implemented according to:
// https://github.com/SFTtech/openage/blob/master/doc/media/smx-files.md and
// https://github.com/SFTtech/openage/blob/master/doc/media/smp-files.md

#pragma pack(push, 1)
struct SMXHeader {
  i16 version;
  i16 numFrames;
  i32 fileSizeComp;
  i32 fileSizeUncomp;
  char comment[16];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SMPHeader {
  u32 version;
  u32 numFrames;
  u32 numAnimations;
  u32 framesPerAnimation;
  u32 checksum;
  u32 fileSize;
  u32 sourceFormat;
  char comment[32];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SMXFrameHeader {
  enum class FrameTypeFlag {
    GraphicLayer = 1 << 0,
    ShadowLayer = 1 << 1,
    OutlineLayer = 1 << 2,
    EightToFiveCompression = 1 << 3,
    UnknownBridgeFlag = 1 << 4,
    Unused1 = 1 << 5,
    Unused2 = 1 << 6,
    Unused3 = 1 << 7,
  };
  
  inline bool HasGraphicLayer() const {
    return frameType & static_cast<u8>(SMXFrameHeader::FrameTypeFlag::GraphicLayer);
  }
  inline bool HasShadowLayer() const {
    return frameType & static_cast<u8>(SMXFrameHeader::FrameTypeFlag::ShadowLayer);
  }
  inline bool HasOutlineLayer() const {
    return frameType & static_cast<u8>(SMXFrameHeader::FrameTypeFlag::OutlineLayer);
  }
  inline bool UsesEightToFiveCompression() const {
    return frameType & static_cast<u8>(SMXFrameHeader::FrameTypeFlag::EightToFiveCompression);
  }
  inline bool HasUnknownBridgeFlag() const {
    return frameType & static_cast<u8>(SMXFrameHeader::FrameTypeFlag::UnknownBridgeFlag);
  }
  
  u8 frameType;
  u8 paletteNumber;
  u32 uncompSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SMXLayerHeader {
  u16 width;
  u16 height;
  u16 hotspotX;
  u16 hotspotY;
  u32 layerLen;
  u32 unknown;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SMPLayerHeader {
  u32 width;
  u32 height;
  u32 hotspotX;
  u32 hotspotY;
  u32 layerType;
  u32 outlineTableOffset;
  u32 cmdTableOffset;
  u32 flags;
};
#pragma pack(pop)

enum class SMXLayerType {
  Graphic = 0,
  Shadow,
  Outline
};

#pragma pack(push, 1)
struct SMPPixel {
  u8  index;
  u8  palette;
  u16 damageModifier;
};
#pragma pack(pop)


inline QRgb GetPalettedPixel(const Palette* palette, u8 paletteSection, u8 colorIndex, bool ignoreAlpha) {
  std::size_t finalIndex = 256 * paletteSection + colorIndex;
  
  if (palette == nullptr) {
    // Encode the index in the pixel value.
    // Set the alpha to the magic value of 254, which the shader will interpret as player color marking.
    CHECK_LT(finalIndex, 65536);
    return qRgba(finalIndex & 0xff, (finalIndex >> 8) & 0xff, 0, 254);
  } else if (finalIndex < palette->size()) {
    const QRgb& rgba = (*palette)[finalIndex];
    return qRgba(qRed(rgba), qGreen(rgba), qBlue(rgba), ignoreAlpha ? 255 : qAlpha(rgba));
  } else {
    std::cout << "AddPalettedPixel(): Index (" << finalIndex << ") is larger than the palette size (" << palette->size() << ")\n";
    return qRgba(0, 0, 0, 0);
  }
}

inline QRgb DecompressNextPixel8To5(const u8*& pixelPtr, int& decompressionState, const Palette* palette, bool ignoreAlpha) {
  QRgb result;
  if (decompressionState == 0) {
    const u8& colorIndex = pixelPtr[0];
    const u8 paletteSection = pixelPtr[1] & 0b11;
    // TODO: Extract and store damage modifiers
    
    result = GetPalettedPixel(palette, paletteSection, colorIndex, ignoreAlpha);
  } else {  // if (decompressionState == 1)
    const u8& colorIndex = ((pixelPtr[2] & 0b11) << 6) | (pixelPtr[1] >> 2);
    const u8 paletteSection = (pixelPtr[2] >> 2) & 0b11;
    // TODO: Extract and store damage modifiers
    
    result = GetPalettedPixel(palette, paletteSection, colorIndex, ignoreAlpha);
    pixelPtr += 5;
  }
  
  decompressionState = (decompressionState + 1) % 2;
  return result;
}

inline QRgb DecompressNextPixel4Plus1(const u8*& pixelPtr, int& decompressionState, const Palette* palette, bool ignoreAlpha) {
  const u8& paletteSections = pixelPtr[4];
  
  QRgb result;
  if (decompressionState == 0) {
    result = GetPalettedPixel(palette, (paletteSections >> 0) & 0b11, pixelPtr[0], ignoreAlpha);
  } else if (decompressionState == 1) {
    result = GetPalettedPixel(palette, (paletteSections >> 2) & 0b11, pixelPtr[1], ignoreAlpha);
  } else if (decompressionState == 2) {
    result = GetPalettedPixel(palette, (paletteSections >> 4) & 0b11, pixelPtr[2], ignoreAlpha);
  } else {  // if (decompressionState == 3)
    result = GetPalettedPixel(palette, paletteSections >> 6, pixelPtr[3], ignoreAlpha);
    pixelPtr += 5;
  }
  
  decompressionState = (decompressionState + 1) % 4;
  return result;
}


Palette LoadPalette(const std::filesystem::path& path);

bool ReadPalettesConf(const char* path, Palettes* palettes);
