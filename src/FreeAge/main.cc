#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include <QImage>


// Type definitions which are more concise and thus easier to read and write (no
// underscore). int is used as-is.
typedef size_t usize;
typedef int64_t i64;
typedef uint64_t u64;
typedef int32_t i32;
typedef uint32_t u32;
typedef int16_t i16;
typedef uint16_t u16;
typedef int8_t i8;
typedef uint8_t u8;


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

inline QRgb DecompressNextPixel8To5(const u8*& pixelPtr, int& decompressionState, const Palette& palette) {
  QRgb result;
  if (decompressionState == 0) {
    const u8& colorIndex = pixelPtr[0];
    const u8 paletteSection = pixelPtr[1] & 0b11;
    // TODO: Extract and store damage modifiers
    
    result = GetPalettedPixel(palette, paletteSection, colorIndex, true);
  } else {  // if (decompressionState == 1)
    const u8& colorIndex = ((pixelPtr[2] & 0b11) << 6) | (pixelPtr[1] >> 2);
    const u8 paletteSection = (pixelPtr[2] >> 2) & 0b11;
    // TODO: Extract and store damage modifiers
    
    result = GetPalettedPixel(palette, paletteSection, colorIndex, true);
    pixelPtr += 5;
  }
  
  decompressionState = (decompressionState + 1) % 2;
  return result;
}

inline QRgb DecompressNextPixel4Plus1(const u8*& pixelPtr, int& decompressionState, const Palette& palette) {
  const u8& paletteSections = pixelPtr[4];
  
  QRgb result;
  if (decompressionState == 0) {
    result = GetPalettedPixel(palette, (paletteSections >> 0) & 0b11, pixelPtr[0], true);
  } else if (decompressionState == 1) {
    result = GetPalettedPixel(palette, (paletteSections >> 2) & 0b11, pixelPtr[1], true);
  } else if (decompressionState == 2) {
    result = GetPalettedPixel(palette, (paletteSections >> 4) & 0b11, pixelPtr[2], true);
  } else {  // if (decompressionState == 3)
    result = GetPalettedPixel(palette, paletteSections >> 6, pixelPtr[3], true);
    pixelPtr += 5;
  }
  
  decompressionState = (decompressionState + 1) % 4;
  return result;
}

bool LoadSMXFile(const char* path, const Palettes& palettes) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    std::cout << "LoadSMXFile(): Cannot open file: " << path << "\n";
    return false;
  }
  
  // Read the header and verify it.
  SMXHeader header;
  if (fread(&header, sizeof(SMXHeader), 1, file) != 1) {
    std::cout << "LoadSMXFile(): Unexpected EOF while trying to read SMXHeader\n";
    fclose(file);
    return false;
  }
  
  if (header.fileDescriptor[0] != 'S' ||
      header.fileDescriptor[1] != 'M' ||
      header.fileDescriptor[2] != 'P' ||
      header.fileDescriptor[3] != 'X') {
    std::cout << "LoadSMXFile(): Header file descriptor is not SMPX\nActual data: "
              << header.fileDescriptor[0] << header.fileDescriptor[1] << header.fileDescriptor[2] << header.fileDescriptor[3] << "\n";
    fclose(file);
    return false;
  }
  
  std::cout << "Version: " << header.version << "\n";
  std::cout << "Frame count: " << header.numFrames << "\n";
  
  // Read the frame header.
  SMXFrameHeader frameHeader;
  if (fread(&frameHeader, sizeof(SMXFrameHeader), 1, file) != 1) {
    std::cout << "LoadSMXFile(): Unexpected EOF while trying to read SMXFrameHeader\n";
    fclose(file);
    return false;
  }
  
  std::cout << "Frame has graphic layer: " << frameHeader.HasGraphicLayer() << "\n";
  std::cout << "Frame has shadow layer: " << frameHeader.HasShadowLayer() << "\n";
  std::cout << "Frame has outline layer: " << frameHeader.HasOutlineLayer() << "\n";
  std::cout << "Frame uses 8to5 compression: " << frameHeader.UsesEightToFiveCompression() << "\n";
  std::cout << "Frame has unknown bridge flag: " << frameHeader.HasUnknownBridgeFlag() << "\n";
  
  // Get the palette for the frame.
  auto paletteIt = palettes.find(frameHeader.paletteNumber);
  if (paletteIt == palettes.end()) {
    std::cout << "LoadSMXFile(): File references an invalid palette (number: " << frameHeader.paletteNumber << ")\n";
    fclose(file);
    return false;
  }
  const Palette& standardPalette = paletteIt->second;
  const Palette& playerColorPalette = palettes.at(55);  // TODO: Hardcoded the blue player palette
  
  // Read the layer header.
  SMXLayerHeader layerHeader;
  if (fread(&layerHeader, sizeof(SMXLayerHeader), 1, file) != 1) {
    std::cout << "LoadSMXFile(): Unexpected EOF while trying to read SMXLayerHeader\n";
    fclose(file);
    return false;
  }
  
  std::cout << "Layer width: " << layerHeader.width << "\n";
  std::cout << "Layer height: " << layerHeader.height << "\n";
  std::cout << "Layer hotspot x: " << layerHeader.hotspotX << "\n";
  std::cout << "Layer hotspot y: " << layerHeader.hotspotY << "\n";
  std::cout << "Layer length: " << layerHeader.layerLen << "\n";
  std::cout << "Layer unknown: " << layerHeader.unknown << "\n";
  
  // Read graphic layer
  if (frameHeader.HasGraphicLayer()) {
    // Read the row edge data.
    std::vector<SMPLayerRowEdge> rowEdges(layerHeader.height);
    for (int row = 0; row < layerHeader.height; ++ row) {
      if (fread(&rowEdges[row], sizeof(SMPLayerRowEdge), 1, file) != 1) {
        std::cout << "LoadSMXFile(): Unexpected EOF while trying to read SMPLayerRowEdge for row " << row << "\n";
        fclose(file);
        return false;
      }
      // std::cout << "Row edge: L: " << rowEdges[row].leftSpace << " R: " << rowEdges[row].rightSpace << "\n";
    }
    
    // Read the command and pixel array length
    u32 commandArrayLen;
    if (fread(&commandArrayLen, sizeof(u32), 1, file) != 1) {
      std::cout << "LoadSMXFile(): Unexpected EOF while trying to read commandArrayLen\n";
      fclose(file);
      return false;
    }
    std::cout << "Command array length: " << commandArrayLen << "\n";
    
    u32 pixelArrayLen;
    if (fread(&pixelArrayLen, sizeof(u32), 1, file) != 1) {
      std::cout << "LoadSMXFile(): Unexpected EOF while trying to read pixelArrayLen\n";
      fclose(file);
      return false;
    }
    std::cout << "Pixel array length: " << pixelArrayLen << "\n";
    
    // Read the command and pixel array data
    std::vector<u8> commandArray(commandArrayLen);
    if (fread(commandArray.data(), sizeof(u8), commandArrayLen, file) != commandArrayLen) {
      std::cout << "LoadSMXFile(): Unexpected EOF while trying to read commandArray\n";
      fclose(file);
      return false;
    }
    
    std::vector<u8> pixelArray(pixelArrayLen);
    if (fread(pixelArray.data(), sizeof(u8), pixelArrayLen, file) != pixelArrayLen) {
      std::cout << "LoadSMXFile(): Unexpected EOF while trying to read pixelArray\n";
      fclose(file);
      return false;
    }
    
    // Build the image.
    QImage graphic(layerHeader.width, layerHeader.height, QImage::Format_ARGB32);
    
    const u8* commandPtr = commandArray.data();
    const u8* pixelPtr = pixelArray.data();
    int decompressionState = 0;
    for (int row = 0; row < layerHeader.height; ++ row) {
      QRgb* out = reinterpret_cast<QRgb*>(graphic.scanLine(row));
      
      // Check for skipped rows
      const SMPLayerRowEdge& edge = rowEdges[row];
      if (edge.leftSpace == 0xFFFF || edge.rightSpace == 0xFFFF) {
        // Row is completely transparent
        for (int col = 0; col < layerHeader.width; ++ col) {
          *out++ = qRgba(0, 0, 0, 0);
        }
        continue;
      }
      
      // Left edge skip
      for (int col = 0; col < edge.leftSpace; ++ col) {
        *out++ = qRgba(0, 0, 0, 0);
      }
      int col = edge.leftSpace;
      
      while (true) {
        // Check the next command.
        u8 command = *commandPtr;
        ++ commandPtr;
        
        u8 commandCode = command & 0b11;
        if (commandCode == 0b00) {
          // Draw *count* transparent pixels.
          u8 count = (command >> 2) + 1;
          for (int i = 0; i < count; ++ i) {
            *out++ = qRgba(0, 0, 0, 0);
          }
          col += count;
        } else if (commandCode == 0b01 || commandCode == 0b10) {
          // Choose the normal or player-color palette depending on the command.
          const Palette& palette = (commandCode == 0b01) ? standardPalette : playerColorPalette;
          
          // Draw *count* pixels from that palette.
          u8 count = (command >> 2) + 1;
          for (int i = 0; i < count; ++ i) {
            if (frameHeader.UsesEightToFiveCompression()) {
              *out++ = DecompressNextPixel8To5(pixelPtr, decompressionState, palette);
            } else {
              *out++ = DecompressNextPixel4Plus1(pixelPtr, decompressionState, palette);
            }
          }
          col += count;
        } else if (commandCode == 0b11) {
          // End of row.
          if (col + edge.rightSpace != layerHeader.width) {
            std::cout << "Warning: Row " << row << ": Pixel count does not match expectation (col: " << col
                      << ", edge.rightSpace: " << edge.rightSpace << ", layerHeader.width: " << layerHeader.width << ")\n";
          }
          for (; col < layerHeader.width; ++ col) {
            *out++ = qRgba(0, 0, 0, 0);
          }
          break;
        }
      }
    }
    
    // TODO: Load other layers / frames
    
    graphic.save("test.png");
  }
  
  // TODO: Other layers
  
  fclose(file);
  return true;
}


Palette LoadPalette(const std::filesystem::path& path) {
  Palette result;
  
  bool hasAlpha = path.extension().string() == ".palx";
  
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    std::cout << "LoadPalette(): Cannot open file: " << path << "\n";
    return result;
  }
  
  fseek(file, 0, SEEK_END);
  std::size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  std::vector<char> data(size);
  if (fread(data.data(), 1, size, file) != size) {
    std::cout << "LoadPalette(): Failed to fully read file: " << path << "\n";
    return result;
  }
  fclose(file);
  
  // Parse the file line-by-line.
  int headerReadState = 0;
  std::size_t currentColor = 0;
  
  std::size_t lineStart = 0;
  for (std::size_t i = 0; i < size; ++ i) {
    if (data[i] == '\r' || data[i] == '\n') {
      // Fully read a line. Parse it.
      std::size_t lineSize = i - lineStart;
      if (lineSize > 0 && data[lineStart] != '#') {
        std::string lineText(data.data() + lineStart, lineSize);
        
        if (headerReadState == 0) {
          if (lineText == "JASC-PAL" || (hasAlpha && lineText == "JASC-PALX")) {
            headerReadState = 1;
          } else {
            std::cout << "LoadPalette(): Unexpected header in file: " << path << "\n";
            return result;
          }
        } else if (headerReadState == 1) {
          if (lineText == "0100") {
            headerReadState = 2;
          } else {
            std::cout << "LoadPalette(): Unexpected header in file: " << path << "\n";
            return result;
          }
        } else if (headerReadState == 2) {
          constexpr int maxNumColors = 99999;
          result.resize(std::min(maxNumColors, atoi(lineText.c_str())));
          headerReadState = 3;
        } else if (hasAlpha && headerReadState == 3) {
          // Line like: $ALPHA 192
          // TODO: Parse this line. What does the number mean?
          headerReadState = 4;
        } else {
          // Read a color.
          int spacePos = lineText.find(' ');
          if (spacePos < 0) {
            std::cout << "LoadPalette(): Failed to parse a color in file: " << path << " (1)\n";
            return result;
          }
          int secondSpacePos = lineText.find(' ', spacePos + 1);
          if (secondSpacePos < 0) {
            std::cout << "LoadPalette(): Failed to parse a color in file: " << path << " (2)\n";
            return result;
          }
          int thirdSpacePos;
          if (hasAlpha) {
            thirdSpacePos = lineText.find(' ', secondSpacePos + 1);
            if (thirdSpacePos < 0) {
              std::cout << "LoadPalette(): Failed to parse a color in file: " << path << " (3)\n";
              return result;
            }
          }
          
          if (currentColor >= result.size()) {
            std::cout << "LoadPalette(): Too many colors in file: " << path << "\n";
            return result;
          }
          result[currentColor] = RGBA(
              atoi(std::string(lineText, 0, spacePos).c_str()),
              atoi(std::string(lineText, spacePos + 1, secondSpacePos - spacePos - 1).c_str()),
              hasAlpha ? atoi(std::string(lineText, secondSpacePos + 1, thirdSpacePos - secondSpacePos - 1).c_str()) : atoi(std::string(lineText, secondSpacePos + 1).c_str()),
              hasAlpha ? atoi(std::string(lineText, thirdSpacePos + 1).c_str()) : 255);
          ++ currentColor;
        }
      }
      
      lineStart = i + 1;
    }
  }
  
  return result;
}


bool ReadPalettesConf(const char* path, Palettes* palettes) {
  std::filesystem::path palettesDir = std::filesystem::path(path).parent_path();
  
  FILE* file = fopen(path, "rb");
  if (!file) {
    std::cout << "LoadSMXFile(): Cannot open file: " << path << "\n";
    return false;
  }
  
  fseek(file, 0, SEEK_END);
  std::size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  std::vector<char> data(size);
  if (fread(data.data(), 1, size, file) != size) {
    std::cout << "LoadSMXFile(): Failed to fully read file: " << path << "\n";
    return false;
  }
  fclose(file);
  
  // Parse the file line-by-line.
  std::size_t lineStart = 0;
  for (std::size_t i = 0; i < size; ++ i) {
    if (data[i] == '\r' || data[i] == '\n') {
      // Fully read a line. Parse it.
      std::size_t lineSize = i - lineStart;
      
      if (lineSize > 0 && data[lineStart] != '/') {
        std::string lineText(data.data() + lineStart, lineSize);
        int commaPos = lineText.find(',');
        if (commaPos < 0) {
          std::cout << "Error: ReadPalettesConf() cannot parse line: " << lineText;
        } else {
          std::string numberText(lineText, 0, commaPos);
          std::string filename(lineText, commaPos + 1);
          int paletteNumber = atoi(numberText.c_str());
          Palette palette = LoadPalette((palettesDir / filename).c_str());
          palettes->insert(std::make_pair(paletteNumber, palette));
        }
      }
      
      lineStart = i + 1;
    }
  }
  
  return true;
}


int main(int /*argc*/, char** /*argv*/) {
  Palettes palettes;
  
  std::filesystem::path commonResourcesPath = "/home/thomas/.local/share/Steam/steamapps/common/AoE2DE/resources/_common";
  
  if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
    return 1;
  }
  
  
  
  // if (!LoadSMXFile((commonResourcesPath / "drs" / "graphics" / "u_cav_warwagon_idleA_x1.smx").c_str(), palettes)) {
  //   return 1;
  // }
  
  // if (!LoadSMXFile((commonResourcesPath / "drs" / "graphics" / "n_tree_palm_x1.smx").c_str(), palettes)) {
  //   return 1;
  // }
  
  if (!LoadSMXFile((commonResourcesPath / "drs" / "graphics" / "b_indi_mill_age3_x1.smx" /*"b_east_castle_age3_x1.smx"*/).c_str(), palettes)) {
    return 1;
  }
  
  return 0;;
}
