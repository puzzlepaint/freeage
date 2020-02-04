#pragma once

#include <filesystem>
#include <iostream>
#include <QImage>
#include <QRgb>
#include <unordered_map>
#include <vector>

#include "FreeAge/FreeAge.h"

struct RGBA {
  inline RGBA() = default;
  
  inline RGBA(u8 r, u8 g, u8 b, u8 a)
      : r(r), g(g), b(b), a(a) {}
  
  u8 r;
  u8 g;
  u8 b;
  u8 a;
};

typedef std::vector<RGBA> Palette;

typedef std::unordered_map<int, Palette> Palettes;


/// A sprite consisting of one or multiple frames, for example loaded from an .smx file.
/// Each frame may have a main graphic, a shadow, and an outline.
/// There is also some additional metadata such as the sprite's center point.
class Sprite {
 public:
  struct Frame {
    struct Layer {
      QImage image;
      int centerX;
      int centerY;
    };
    
    Layer graphic;
    Layer shadow;
    Layer outline;
  };
  
  Sprite() = default;
  
  bool LoadFromFile(const char* path, const Palettes& palettes);
  
  inline int NumFrames() const { return frames.size(); }
  inline const Frame& frame(int index) { return frames[index]; }
  
 private:
  std::vector<Frame> frames;
};


// Note: SMX parsing implemented according to:
// https://github.com/SFTtech/openage/blob/master/doc/media/smx-files.md

#pragma pack(push, 1)
struct SMXHeader {
  char fileDescriptor[4];
  i16 version;
  i16 numFrames;
  i32 fileSizeComp;
  i32 fileSizeUncomp;
  char comment[16];
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

enum class SMXLayerType {
  Graphic = 0,
  Shadow,
  Outline
};

#pragma pack(push, 1)
struct SMPLayerRowEdge {
  u16 leftSpace;
  u16 rightSpace;
};
#pragma pack(pop)


inline QRgb GetPalettedPixel(const Palette& palette, u8 paletteSection, u8 colorIndex, bool ignoreAlpha) {
  std::size_t finalIndex = 256 * paletteSection + colorIndex;
  
  if (finalIndex < palette.size()) {
    const RGBA& rgba = palette[finalIndex];
    return qRgba(rgba.r, rgba.g, rgba.b, ignoreAlpha ? 255 : rgba.a);
  } else {
    std::cout << "AddPalettedPixel(): Index (" << finalIndex << ") is larger than the palette size (" << palette.size() << ")\n";
    return qRgba(0, 0, 0, 0);
  }
}

inline QRgb DecompressNextPixel8To5(const u8*& pixelPtr, int& decompressionState, const Palette& palette, bool ignoreAlpha) {
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

inline QRgb DecompressNextPixel4Plus1(const u8*& pixelPtr, int& decompressionState, const Palette& palette, bool ignoreAlpha) {
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


QImage LoadSMXGraphicLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    bool usesEightToFiveCompression,
    const Palette& standardPalette,
    const Palette& playerColorPalette,
    FILE* file);

QImage LoadSMXShadowLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    FILE* file);

QImage LoadSMXOutlineLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    FILE* file);

bool LoadSMXLayer(
    bool usesEightToFiveCompression,
    const Palette& standardPalette,
    const Palette& playerColorPalette,
    SMXLayerType layerType,
    Sprite::Frame::Layer* layer,
    FILE* file);


Palette LoadPalette(const std::filesystem::path& path);


bool ReadPalettesConf(const char* path, Palettes* palettes);
