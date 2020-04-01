#include "FreeAge/client/sprite.hpp"

#include <filesystem>

#include <mango/image/image.hpp>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/timing.hpp"
#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/shader_program.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/sprite_atlas.hpp"
#include "FreeAge/client/texture.hpp"

QImage LoadSMXGraphicLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    bool usesEightToFiveCompression,
    int pixelBorder,
    const Palette& standardPalette,
    FILE* file) {
  // Read the command and pixel array length
  u32 commandArrayLen;
  if (fread(&commandArrayLen, sizeof(u32), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read commandArrayLen";
    return QImage();
  }
  
  u32 pixelArrayLen;
  if (fread(&pixelArrayLen, sizeof(u32), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read pixelArrayLen";
    return QImage();
  }
  
  // Read the command and pixel array data
  std::vector<u8> commandArray(commandArrayLen);
  if (fread(commandArray.data(), sizeof(u8), commandArrayLen, file) != commandArrayLen) {
    LOG(ERROR) << "Unexpected EOF while trying to read commandArray";
    return QImage();
  }
  
  std::vector<u8> pixelArray(pixelArrayLen);
  if (fread(pixelArray.data(), sizeof(u8), pixelArrayLen, file) != pixelArrayLen) {
    LOG(ERROR) << "Unexpected EOF while trying to read pixelArray";
    return QImage();
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
    for (int col = 0; col < edge.leftSpace + pixelBorder; ++ col) {
      *out++ = qRgba(0, 0, 0, 0);
    }
    int col = edge.leftSpace + pixelBorder;
    
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
        const Palette* palette = (commandCode == 0b01) ? &standardPalette : nullptr;
        
        // Draw *count* pixels from that palette.
        u8 count = (command >> 2) + 1;
        bool ignoreAlpha = true;  /*layerType == SMXLayerType::Graphic*/
        for (int i = 0; i < count; ++ i) {
          if (usesEightToFiveCompression) {
            *out = DecompressNextPixel8To5(pixelPtr, decompressionState, palette, ignoreAlpha);
          } else {
            *out = DecompressNextPixel4Plus1(pixelPtr, decompressionState, palette, ignoreAlpha);
          }
          
          ++ out;
        }
        col += count;
      } else if (commandCode == 0b11) {
        // End of row.
        if (col + edge.rightSpace + pixelBorder != layerHeader.width) {
          LOG(WARNING) << "Row " << row << ": Pixel count does not match expectation (col: " << col
                       << ", edge.rightSpace: " << edge.rightSpace << ", layerHeader.width: " << layerHeader.width << ")";
        }
        for (; col < layerHeader.width; ++ col) {
          *out++ = qRgba(0, 0, 0, 0);
        }
        break;
      }
    }
  }
  
  return graphic;
}

QImage LoadSMXShadowLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    FILE* file) {
  // Read the combined command and data array
  u32 dataLen;
  if (fread(&dataLen, sizeof(u32), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read dataLen";
    return QImage();
  }
  
  std::vector<u8> data(dataLen);
  if (fread(data.data(), sizeof(u8), dataLen, file) != dataLen) {
    LOG(ERROR) << "Unexpected EOF while trying to read data";
    return QImage();
  }
  
  // Build the image.
  QImage graphic(layerHeader.width, layerHeader.height, QImage::Format_Grayscale8);
  
  const u8* dataPtr = data.data();
  for (int row = 0; row < layerHeader.height; ++ row) {
    u8* out = graphic.scanLine(row);
    
    // Check for skipped rows
    const SMPLayerRowEdge& edge = rowEdges[row];
    if (edge.leftSpace == 0xFFFF || edge.rightSpace == 0xFFFF) {
      // Row is completely transparent
      for (int col = 0; col < layerHeader.width; ++ col) {
        *out++ = 0;
      }
      continue;
    }
    
    // Left edge skip
    for (int col = 0; col < edge.leftSpace; ++ col) {
      *out++ = 0;
    }
    int col = edge.leftSpace;
    
    while (true) {
      // Check the next command.
      u8 command = *dataPtr;
      ++ dataPtr;
      
      u8 commandCode = command & 0b11;
      if (commandCode == 0b00) {
        // Draw *count* transparent pixels.
        u8 count = (command >> 2) + 1;
        for (int i = 0; i < count; ++ i) {
          *out++ = 0;
        }
        col += count;
      } else if (commandCode == 0b01) {
        // Draw *count* pixels.
        u8 count = (command >> 2) + 1;
        for (int i = 0; i < count; ++ i) {
          *out++ = *dataPtr++;
        }
        col += count;
      } else if (commandCode == 0b11) {
        // End of row.
        // NOTE: We account for what seems like a bug here, where there is one pixel of data missing.
        if ((col + edge.rightSpace != layerHeader.width) &&
            (col + edge.rightSpace + 1 != layerHeader.width)) {
          LOG(WARNING) << "Row " << row << ": Pixel count does not match expectation (col: " << col
                       << ", edge.rightSpace: " << edge.rightSpace << ", layerHeader.width: " << layerHeader.width << ")";
        }
        for (; col < layerHeader.width; ++ col) {
          *out++ = 0;
        }
        break;
      } else {
        LOG(ERROR) << "Unexpected drawing code 0b10";
        return QImage();
      }
    }
  }
  
  return graphic;
}

QImage LoadSMXOutlineLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    FILE* file) {
  // Read the combined command and data array
  u32 dataLen;
  if (fread(&dataLen, sizeof(u32), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read dataLen";
    return QImage();
  }
  
  std::vector<u8> data(dataLen);
  if (fread(data.data(), sizeof(u8), dataLen, file) != dataLen) {
    LOG(ERROR) << "Unexpected EOF while trying to read data";
    return QImage();
  }
  
  // Build the image.
  QImage graphic(layerHeader.width, layerHeader.height, QImage::Format_Grayscale8);
  
  const u8* dataPtr = data.data();
  for (int row = 0; row < layerHeader.height; ++ row) {
    u8* out = graphic.scanLine(row);
    
    // Check for skipped rows
    const SMPLayerRowEdge& edge = rowEdges[row];
    if (edge.leftSpace == 0xFFFF || edge.rightSpace == 0xFFFF) {
      // Row is completely transparent
      for (int col = 0; col < layerHeader.width; ++ col) {
        *out++ = 0;
      }
      continue;
    }
    
    // Left edge skip
    for (int col = 0; col < edge.leftSpace; ++ col) {
      *out++ = 0;
    }
    int col = edge.leftSpace;
    
    while (true) {
      // Check the next command.
      u8 command = *dataPtr;
      ++ dataPtr;
      
      u8 commandCode = command & 0b11;
      if (commandCode == 0b00) {
        // Draw *count* transparent pixels.
        u8 count = (command >> 2) + 1;
        for (int i = 0; i < count; ++ i) {
          *out++ = 0;
        }
        col += count;
      } else if (commandCode == 0b01) {
        // Draw *count* pixels.
        u8 count = (command >> 2) + 1;
        for (int i = 0; i < count; ++ i) {
          *out++ = 255;
        }
        col += count;
      } else if (commandCode == 0b11) {
        // End of row.
        if (col + edge.rightSpace != layerHeader.width) {
          LOG(WARNING) << "Row " << row << ": Pixel count does not match expectation (col: " << col
                       << ", edge.rightSpace: " << edge.rightSpace << ", layerHeader.width: " << layerHeader.width << ")";
        }
        for (; col < layerHeader.width; ++ col) {
          *out++ = 0;
        }
        break;
      } else {
        LOG(ERROR) << "Unexpected drawing code 0b10";
        return QImage();
      }
    }
  }
  
  return graphic;
}

bool LoadSMXLayer(
    bool usesEightToFiveCompression,
    const Palette& standardPalette,
    SMXLayerType layerType,
    Sprite::Frame::Layer* layer,
    std::vector<SMPLayerRowEdge>* rowEdges,
    FILE* file) {
  // Read the layer header.
  SMXLayerHeader layerHeader;
  if (fread(&layerHeader, sizeof(SMXLayerHeader), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read SMXLayerHeader";
    return false;
  }
  
  // LOG(INFO) << "Layer width: " << layerHeader.width;
  // LOG(INFO) << "Layer height: " << layerHeader.height;
  // LOG(INFO) << "Layer hotspot x: " << layerHeader.hotspotX;
  // LOG(INFO) << "Layer hotspot y: " << layerHeader.hotspotY;
  // LOG(INFO) << "Layer length: " << layerHeader.layerLen;
  // LOG(INFO) << "Layer unknown: " << layerHeader.unknown;
  
  int pixelBorder = (layerType == SMXLayerType::Graphic) ? 1 : 0;
  layerHeader.width += 2 * pixelBorder;
  layerHeader.height += 2 * pixelBorder;
  layerHeader.hotspotX += pixelBorder;
  layerHeader.hotspotY += pixelBorder;
  
  layer->centerX = layerHeader.hotspotX;
  layer->centerY = layerHeader.hotspotY;
  
  // Read the row edge data.
  rowEdges->resize(layerHeader.height);
  for (int i = 0; i < pixelBorder; ++ i) {
    (*rowEdges)[i].leftSpace = 0xFFFF;
    (*rowEdges)[i].rightSpace = 0xFFFF;
    (*rowEdges)[rowEdges->size() - 1 - i].leftSpace = 0xFFFF;
    (*rowEdges)[rowEdges->size() - 1 - i].rightSpace = 0xFFFF;
  }
  for (int row = 0; row < layerHeader.height - 2 * pixelBorder; ++ row) {
    if (fread(&(*rowEdges)[row + pixelBorder], sizeof(SMPLayerRowEdge), 1, file) != 1) {
      LOG(ERROR) << "Unexpected EOF while trying to read SMPLayerRowEdge for row " << row;
      return false;
    }
    // LOG(INFO) << "Row edge: L: " << rowEdges[row].leftSpace << " R: " << rowEdges[row].rightSpace;
  }
  
  if (layerType == SMXLayerType::Graphic) {
    layer->image = LoadSMXGraphicLayer(
        layerHeader,
        *rowEdges,
        usesEightToFiveCompression,
        pixelBorder,
        standardPalette,
        file);
  } else if (layerType == SMXLayerType::Shadow) {
    layer->image = LoadSMXShadowLayer(
        layerHeader,
        *rowEdges,
        file);
  } else if (layerType == SMXLayerType::Outline) {
    layer->image = LoadSMXOutlineLayer(
        layerHeader,
        *rowEdges,
        file);
  }
  if (layer->image.isNull()) {
    return false;
  }
  
  // Store the image dimensions for the time when the image got unloaded
  // (i.e., once it was transferred to the GPU).
  layer->imageWidth = layer->image.width();
  layer->imageHeight = layer->image.height();
  
  return true;
}


Palette LoadPalette(const std::filesystem::path& path) {
  Palette result;
  
  bool hasAlpha = path.extension().string() == ".palx";
  
  FILE* file = fopen(path.string().c_str(), "rb");
  if (!file) {
    LOG(ERROR) << "Cannot open file: " << path;
    return result;
  }
  
  fseek(file, 0, SEEK_END);
  std::size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  std::vector<char> data(size);
  if (fread(data.data(), 1, size, file) != size) {
    LOG(ERROR) << "Failed to fully read file: " << path;
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
            LOG(ERROR) << "Unexpected header in file: " << path;
            return result;
          }
        } else if (headerReadState == 1) {
          if (lineText == "0100") {
            headerReadState = 2;
          } else {
            LOG(ERROR) << "Unexpected header in file: " << path;
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
            LOG(ERROR) << "Failed to parse a color in file: " << path << " (1)";
            return result;
          }
          int secondSpacePos = lineText.find(' ', spacePos + 1);
          if (secondSpacePos < 0) {
            LOG(ERROR) << "Failed to parse a color in file: " << path << " (2)";
            return result;
          }
          int thirdSpacePos;
          if (hasAlpha) {
            thirdSpacePos = lineText.find(' ', secondSpacePos + 1);
            if (thirdSpacePos < 0) {
              LOG(ERROR) << "Failed to parse a color in file: " << path << " (3)";
              return result;
            }
          }
          
          if (currentColor >= result.size()) {
            LOG(ERROR) << "Too many colors in file: " << path;
            return result;
          }
          result[currentColor] = qRgba(
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
    LOG(ERROR) << "Cannot open file: " << path;
    return false;
  }
  
  fseek(file, 0, SEEK_END);
  std::size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  std::vector<char> data(size);
  if (fread(data.data(), 1, size, file) != size) {
    LOG(ERROR) << "Failed to fully read file: " << path;
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
          LOG(ERROR) << "Cannot parse line: " << lineText;
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

/// Performs a little inpainting in the shadow layer at pixels that are covered by the graphic layer.
/// This makes sprites look a bit better when zoomed in, since there will be a shadow behind objects.
/// Without the inpainting, there is no shadow behind objects, so the bilinear texture interpolation
/// causes the shadow to fade out towards the object, making it have something like a bright halo artifact.
void InpaintShadowBehindGraphic(Sprite::Frame::Layer* shadow, const Sprite::Frame::Layer& graphic) {
  int offsetX = graphic.centerX - shadow->centerX;
  int offsetY = graphic.centerY - shadow->centerY;
  
  for (int y = 0; y < shadow->imageHeight; ++ y) {
    int graphicY = y + offsetY;
    if (graphicY < 0 || graphicY >= graphic.imageHeight) {
      continue;
    }
    const QRgb* graphicScanLine = reinterpret_cast<const QRgb*>(graphic.image.scanLine(graphicY));
    u8* outputScanLine = reinterpret_cast<u8*>(shadow->image.scanLine(y));
    
    for (int x = 0; x < shadow->imageWidth; ++ x) {
      // Get the corresponding pixel in the graphic sprite (may be out of bounds though).
      int graphicX = x + offsetX;
      if (graphicX < 0 || graphicX >= graphic.imageWidth) {
        continue;
      }
      
      // Check whether this pixel is opaque in the graphic sprite. If not, continue.
      if (qAlpha(graphicScanLine[graphicX]) < 127) {
        continue;
      }
      
      // Inpaint this pixel in the shadow sprite.
      // TODO: Use integral images for better performance
      int minX = std::max(0, x - 1);
      int minY = std::max(0, y - 1);
      int maxX = std::min(shadow->imageWidth - 1, x + 1);
      int maxY = std::min(shadow->imageHeight - 1, y + 1);
      
      int sum = 0;
      int count = 0;
      for (int sy = minY; sy <= maxY; ++ sy) {
        int graphicSY = sy + offsetY;
        if (graphicSY < 0 || graphicSY >= graphic.imageHeight) {
          continue;
        }
        const QRgb* graphicScanLineS = reinterpret_cast<const QRgb*>(graphic.image.scanLine(graphicSY));
        
        for (int sx = minX; sx <= maxX; ++ sx) {
          int graphicSX = sx + offsetX;
          if (graphicSX < 0 || graphicSX >= graphic.imageWidth) {
            continue;
          }
          
          if (qAlpha(graphicScanLineS[graphicSX]) >= 127) {
            continue;
          }
          
          sum += shadow->image.scanLine(sy)[sx];
          ++ count;
        }
      }
      
      if (count > 0) {
        float factor = 1.f / count;
        outputScanLine[x] = sum * factor + 0.5f;
      }
    }
  }
}

/// Paints the outline data into the graphic layer, setting the alpha value of pixels with an outline to:
/// * 1, if the pixel's actual alpha value was 0
/// * 253, if the pixel's actual alpha value was 255
/// * 252, if the pixel's actual alpha value was 254
void PaintOutlineIntoGraphic(Sprite::Frame::Layer* graphic, const Sprite::Frame::Layer& outline) {
  int offsetX = graphic->centerX - outline.centerX;
  int offsetY = graphic->centerY - outline.centerY;
  
  for (int y = 0; y < outline.imageHeight; ++ y) {
    int graphicY = y + offsetY;
    if (graphicY < 0 || graphicY >= graphic->imageHeight) {
      continue;
    }
    QRgb* graphicScanLine = reinterpret_cast<QRgb*>(graphic->image.scanLine(graphicY));
    const u8* outlineScanLine = reinterpret_cast<const u8*>(outline.image.scanLine(y));
    
    for (int x = 0; x < outline.imageWidth; ++ x) {
      if (outlineScanLine[x] != 255) {
        continue;
      }
      
      // Get the corresponding pixel in the graphic sprite (may be out of bounds though).
      int graphicX = x + offsetX;
      if (graphicX < 0 || graphicX >= graphic->imageWidth) {
        continue;
      }
      
      QRgb value = graphicScanLine[graphicX];
      int alpha = qAlpha(value);
      if (alpha == 0) {
        alpha = 1;
      } else if (alpha == 255) {
        alpha = 253;
      } else {  // if (alpha == 254) {
        alpha = 252;
      }
      
      graphicScanLine[graphicX] = qRgba(qRed(value), qGreen(value), qBlue(value), alpha);
    }
  }
}

bool Sprite::LoadFromFile(const char* path, const Palettes& palettes) {
  int pathLen = strlen(path);
  if (pathLen > 3 &&
      (path[pathLen - 3] == 'P' || path[pathLen - 3] == 'p') &&
      (path[pathLen - 2] == 'N' || path[pathLen - 2] == 'n') &&
      (path[pathLen - 1] == 'G' || path[pathLen - 1] == 'g')) {
    return LoadFromPNGFiles(path);
  }
  
  FILE* file = fopen(path, "rb");
  if (!file) {
    LOG(ERROR) << "Cannot open file: " << path;
    return false;
  }
  std::shared_ptr<FILE> fileCloser(file, [&](FILE* file) { fclose(file); });
  
  // Read the file descriptor.
  char fileDescriptor[4];
  if (fread(fileDescriptor, sizeof(char), 4, file) != 4) {
    LOG(ERROR) << "Unexpected EOF while trying to read file descriptor";
    return false;
  }
  
  if (fileDescriptor[0] == 'S' &&
      fileDescriptor[1] == 'M' &&
      fileDescriptor[2] == 'P' &&
      fileDescriptor[3] == 'X') {
    return LoadFromSMXFile(file, palettes);
  } else if (fileDescriptor[0] == 'S' &&
             fileDescriptor[1] == 'M' &&
             fileDescriptor[2] == 'P' &&
             fileDescriptor[3] == '$') {
    return LoadFromSMPFile(file, palettes);
  } else {
    LOG(ERROR) << "Header file descriptor is not SMPX or SMP$\nActual data: "
               << fileDescriptor[0] << fileDescriptor[1] << fileDescriptor[2] << fileDescriptor[3];
    return false;
  }
}

bool Sprite::LoadFromSMXFile(FILE* file, const Palettes& palettes) {
  // Read the header.
  SMXHeader smxHeader;
  if (fread(&smxHeader, sizeof(SMXHeader), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read SMXHeader";
    return false;
  }
  
  frames.resize(smxHeader.numFrames);
  for (int frameIdx = 0; frameIdx < smxHeader.numFrames; ++ frameIdx) {
    Frame& frame = frames[frameIdx];
    
    // Read the frame header.
    SMXFrameHeader frameHeader;
    if (fread(&frameHeader, sizeof(SMXFrameHeader), 1, file) != 1) {
      LOG(ERROR) << "Unexpected EOF while trying to read SMXFrameHeader";
      return false;
    }
    
    // LOG(INFO) << "Frame has graphic layer: " << frameHeader.HasGraphicLayer();
    // LOG(INFO) << "Frame has shadow layer: " << frameHeader.HasShadowLayer();
    // LOG(INFO) << "Frame has outline layer: " << frameHeader.HasOutlineLayer();
    // LOG(INFO) << "Frame uses 8to5 compression: " << frameHeader.UsesEightToFiveCompression();
    // LOG(INFO) << "Frame has unknown bridge flag: " << frameHeader.HasUnknownBridgeFlag();
    
    // Get the palette for the frame.
    auto paletteIt = palettes.find(frameHeader.paletteNumber);
    if (paletteIt == palettes.end()) {
      LOG(ERROR) << "File references an invalid palette (number: " << frameHeader.paletteNumber << ")";
      return false;
    }
    const Palette& standardPalette = paletteIt->second;
    
    // Read graphic layer
    if (frameHeader.HasGraphicLayer()) {
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          SMXLayerType::Graphic,
          &frame.graphic,
          &frame.rowEdges,
          file)) {
        return false;
      }
    }
    
    // Read shadow layer
    if (frameHeader.HasShadowLayer()) {
      std::vector<SMPLayerRowEdge> rowEdges;
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          SMXLayerType::Shadow,
          &frame.shadow,
          &rowEdges,
          file)) {
        return false;
      }
      
      InpaintShadowBehindGraphic(&frame.shadow, frame.graphic);
    }
    
    // Read outline layer
    if (frameHeader.HasOutlineLayer()) {
      std::vector<SMPLayerRowEdge> rowEdges;
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          SMXLayerType::Outline,
          &frame.outline,
          &rowEdges,
          file)) {
        return false;
      }
      
      PaintOutlineIntoGraphic(&frame.graphic, frame.outline);
      frame.outline.image = QImage();  // unload outline image data
    }
  }
  
  return true;
}

bool Sprite::LoadFromSMPFile(FILE* file, const Palettes& palettes) {
  SMPHeader smpHeader;
  if (fread(&smpHeader, sizeof(SMPHeader), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read SMPHeader";
    return false;
  }
  
  // Read frame offsets.
  std::vector<u32> frameOffsets(smpHeader.numFrames);
  if (fread(frameOffsets.data(), sizeof(u32), smpHeader.numFrames, file) != smpHeader.numFrames) {
    LOG(ERROR) << "Unexpected EOF while trying to read SMP frame offsets";
    return false;
  }
  
  frames.resize(smpHeader.numFrames);
  for (usize frameIdx = 0; frameIdx < smpHeader.numFrames; ++ frameIdx) {
    Frame& frame = frames[frameIdx];
    fseek(file, frameOffsets[frameIdx], SEEK_SET);
    
    // Read frame header
    u32 unused[7];
    if (fread(unused, sizeof(u32), 7, file) != 7) {
      LOG(ERROR) << "Unexpected EOF while trying to read unused data in SMP";
      return false;
    }
    
    u32 numLayers;
    if (fread(&numLayers, sizeof(u32), 1, file) != 1) {
      LOG(ERROR) << "Unexpected EOF while trying to read SMP frameHeader";
      return false;
    }
    
    // Read layer headers
    std::vector<SMPLayerHeader> layerHeaders(numLayers);
    if (fread(layerHeaders.data(), sizeof(SMPLayerHeader), numLayers, file) != numLayers) {
      LOG(ERROR) << "Unexpected EOF while trying to read SMPLayerHeaders";
      return false;
    }
    
    for (usize layer = 0; layer < numLayers; ++ layer) {
      const SMPLayerHeader& layerHeader = layerHeaders[layer];
      fseek(file, frameOffsets[frameIdx] + layerHeader.outlineTableOffset, SEEK_SET);
      
      // LOG(WARNING) << "Layer width: " << layerHeader.width;
      // LOG(WARNING) << "Layer height: " << layerHeader.height;
      // LOG(WARNING) << "Layer hotspot x: " << layerHeader.hotspotX;
      // LOG(WARNING) << "Layer hotspot y: " << layerHeader.hotspotY;
      
      // Read the row edge data.
      constexpr int pixelBorder = 0;
      std::vector<SMPLayerRowEdge> rowEdges(layerHeader.height);
      for (int i = 0; i < pixelBorder; ++ i) {
        rowEdges[i].leftSpace = 0xFFFF;
        rowEdges[i].rightSpace = 0xFFFF;
        rowEdges[rowEdges.size() - 1 - i].leftSpace = 0xFFFF;
        rowEdges[rowEdges.size() - 1 - i].rightSpace = 0xFFFF;
      }
      for (usize row = 0; row < layerHeader.height - 2 * pixelBorder; ++ row) {
        if (fread(&rowEdges[row + pixelBorder], sizeof(SMPLayerRowEdge), 1, file) != 1) {
          LOG(ERROR) << "Unexpected EOF while trying to read SMPLayerRowEdge for row " << row;
          return false;
        }
        // LOG(INFO) << "Row edge: L: " << rowEdges[row].leftSpace << " R: " << rowEdges[row].rightSpace;
      }
      
      if (layerHeader.layerType == 0x02) {
        // Graphic layer.
        Sprite::Frame::Layer* layer = &frame.graphic;
        
        layer->centerX = layerHeader.hotspotX;
        layer->centerY = layerHeader.hotspotY;
        
        // LOG(WARNING) << "Gfx layer sized " << layerHeader.width << " x " << layerHeader.height;
        
        fseek(file, frameOffsets[frameIdx] + layerHeader.cmdTableOffset, SEEK_SET);
        
        std::vector<u32> smpCommandOffsets(layerHeader.height);
        if (fread(smpCommandOffsets.data(), sizeof(u32), layerHeader.height, file) != layerHeader.height) {
          LOG(ERROR) << "Unexpected EOF while trying to read smpCommandOffsets";
          return false;
        }
        
        // NOTE: We only seek to the first offset and then assume that the following rows are stored sequentially.
        fseek(file, frameOffsets[frameIdx] + smpCommandOffsets[0], SEEK_SET);
        
        constexpr bool ignoreAlpha = true;  /*layerType == SMXLayerType::Graphic*/
        
        // Build the image.
        QImage graphic(layerHeader.width, layerHeader.height, QImage::Format_ARGB32);
        
        for (usize row = 0; row < layerHeader.height; ++ row) {
          QRgb* out = reinterpret_cast<QRgb*>(graphic.scanLine(row));
          
          // Check for skipped rows
          const SMPLayerRowEdge& edge = rowEdges[row];
          if (edge.leftSpace == 0xFFFF || edge.rightSpace == 0xFFFF) {
            // Row is completely transparent
            for (usize col = 0; col < layerHeader.width; ++ col) {
              *out++ = qRgba(0, 0, 0, 0);
            }
            continue;
          }
          
          // Left edge skip
          for (int col = 0; col < edge.leftSpace + pixelBorder; ++ col) {
            *out++ = qRgba(0, 0, 0, 0);
          }
          u32 col = edge.leftSpace + pixelBorder;
          
          while (true) {
            // Read the next command.
            u8 command;
            if (fread(&command, sizeof(u8), 1, file) != 1) {
              LOG(ERROR) << "Unexpected EOF while trying to read SMP drawing command";
              return false;
            }
            
            u8 commandCode = command & 0b11;
            if (commandCode == 0b00) {
              // Draw *count* transparent pixels.
              u8 count = (command >> 2) + 1;
              for (int i = 0; i < count; ++ i) {
                *out++ = qRgba(0, 0, 0, 0);
              }
              col += count;
            } else if (commandCode == 0b01 || commandCode == 0b10) {
              u8 count = (command >> 2) + 1;
              
              for (int i = 0; i < count; ++ i) {
                SMPPixel pixel;
                if (fread(&pixel, sizeof(SMPPixel), 1, file) != 1) {
                  LOG(ERROR) << "Unexpected EOF while trying to read an SMPPixel";
                  return false;
                }
                
                int paletteIndex = pixel.palette >> 2;
                int paletteSection = pixel.palette & 0b11;
                
                // TODO: If we load a lot of SMPs, we might want to optimize the unordered_map access here (cache the last used palette and re-use it if it is the same for the next pixel)
                *out = GetPalettedPixel((commandCode == 0b01) ? &palettes.at(paletteIndex) : nullptr, paletteSection, pixel.index, ignoreAlpha);
                ++ out;
              }
              col += count;
            } else if (commandCode == 0b11) {
              // End of row.
              if (col + edge.rightSpace + pixelBorder != layerHeader.width) {
                LOG(WARNING) << "Row " << row << ": Pixel count does not match expectation (col: " << col
                              << ", edge.rightSpace: " << edge.rightSpace << ", layerHeader.width: " << layerHeader.width << ")";
              }
              for (; col < layerHeader.width; ++ col) {
                *out++ = qRgba(0, 0, 0, 0);
              }
              break;
            }
          }
        }
        
        layer->image = graphic;
        layer->imageWidth = layer->image.width();
        layer->imageHeight = layer->image.height();
      } else if (layerHeader.layerType == 0x04) {
        // Shadow layer.
        // TODO: Not implemented yet.
      } else if (layerHeader.layerType == 0x08 || layerHeader.layerType == 0x10) {
        // Outline layer.
        // TODO: Not implemented yet.
      } else {
        LOG(ERROR) << "Unknown layer type in SMP file: " << layerHeader.layerType;
      }
    }
  }
  
  return true;
}

bool Sprite::LoadFromPNGFiles(const char* path) {
  int frameIdx = 0;
  while (true) {
    char pathBuffer[512];
    sprintf(pathBuffer, path, frameIdx);
    if (!std::filesystem::exists(pathBuffer)) {
      break;
    }
    
    // Load the frame from the PNG image.
    // We assume that the sprite center is in the center of the image.
    mango::Bitmap bitmap(pathBuffer, mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
    
    // Get the bounding rect of pixels having alpha > 0
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::lowest();
    int maxY = std::numeric_limits<int>::lowest();
    
    for (int y = 0; y < bitmap.height; ++ y) {
      u8* rowPtr = reinterpret_cast<u8*>(bitmap.address<u32>(0, y));
      for (int x = 0; x < bitmap.width; ++ x) {
        int alpha = rowPtr[4 * x + 3];
        if (alpha > 0) {
          minX = std::min(minX, x);
          minY = std::min(minY, y);
          maxX = std::max(maxX, x);
          maxY = std::max(maxY, y);
        }
      }
    }
    
    // Extend the bounding rect by one pixel on each side to leave space for bilinear interpolation
    minX = std::max(0, minX - 1);
    minY = std::max(0, minY - 1);
    maxX = std::min(bitmap.width - 1, maxX + 1);
    maxY = std::min(bitmap.height - 1, maxY + 1);
    
    int width = maxX - minX + 1;
    int height = maxY - minY + 1;
    
    // Create a sprite frame out of the pixels in the bounding rect
    frames.emplace_back();
    Frame& frame = frames.back();
    Frame::Layer& graphic = frame.graphic;
    
    graphic.imageWidth = width;
    graphic.imageHeight = height;
    graphic.centerX = (bitmap.width / 2) - minX;
    graphic.centerY = (bitmap.height / 2) - minY;
    
    graphic.image = QImage(width, height, QImage::Format_ARGB32);
    for (int y = minY; y <= maxY; ++ y) {
      u32* inRowPtr = bitmap.address<u32>(0, y);
      u32* outRowPtr = reinterpret_cast<u32*>(graphic.image.scanLine(y - minY));
      for (int x = minX; x <= maxX; ++ x) {
        outRowPtr[x - minX] = inRowPtr[x];
      }
    }
    
    ++ frameIdx;
  }
  
  return !frames.empty();
}


SpriteAndTextures* SpriteManager::GetOrLoad(const char* path, const char* cachePath, const Palettes& palettes) {
  auto it = loadedSprites.find(path);
  if (it != loadedSprites.end()) {
    ++ it->second->referenceCount;
    return it->second;
  }
  
  // Load the sprite.
  SpriteAndTextures* newSprite = new SpriteAndTextures();
  newSprite->referenceCount = 1;
  if (!LoadSpriteAndTexture(path, cachePath, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, &newSprite->sprite, &newSprite->graphicTexture, &newSprite->shadowTexture, palettes)) {
    LOG(ERROR) << "Failed to load sprite: " << path;
    return nullptr;
  }
  
  loadedSprites.insert(std::make_pair(path, newSprite));
  return newSprite;
}

void SpriteManager::Dereference(SpriteAndTextures* sprite) {
  -- sprite->referenceCount;
  if (sprite->referenceCount > 0) {
    return;
  }
  
  for (auto it = loadedSprites.begin(), end = loadedSprites.end(); it != end; ++ it) {
    if (it->second == sprite) {
      loadedSprites.erase(it);
      delete sprite;
      return;
    }
  }
  
  LOG(ERROR) << "The reference count for a sprite reached zero, but it could not be found in loadedSprites to remove it from there.";
  delete sprite;
}

SpriteManager::~SpriteManager() {
  for (const auto& item : loadedSprites) {
    LOG(ERROR) << "Sprite still loaded on SpriteManager destruction: " << item.first << " (references: " << item.second->referenceCount << ")";
  }
}


bool LoadSpriteAndTexture(const char* path, const char* cachePath, int wrapMode, int /*magFilter*/, int /*minFilter*/, Sprite* sprite, Texture* graphicTexture, Texture* shadowTexture, const Palettes& palettes) {
  // TODO magFilter and minFilter are unused here
  
  if (!sprite->LoadFromFile(path, palettes)) {
    LOG(ERROR) << "Failed to load sprite from " << path;
    return false;
  }
  
  // Create a sprite atlas texture containing all frames of the SMX animation.
  // TODO: This generally takes a LOT of memory. We probably want to do a dense packing of the images using
  //       non-rectangular geometry to save some more space.
  for (int graphicOrShadow = 0; graphicOrShadow < 2; ++ graphicOrShadow) {
    if (graphicOrShadow == 1 && !sprite->HasShadow()) {
      continue;
    }
    SpriteAtlas::Mode mode = (graphicOrShadow == 0) ? SpriteAtlas::Mode::Graphic : SpriteAtlas::Mode::Shadow;
    Texture* texture = (graphicOrShadow == 0) ? graphicTexture : shadowTexture;
    
    SpriteAtlas atlas(mode);
    atlas.AddSprite(sprite);
    
    constexpr int pixelBorder = 0;
    
    std::string cacheFilePath = std::string(cachePath) + ((graphicOrShadow == 0) ? ".graphic" : ".shadow");
    bool loaded = false;
    if (std::filesystem::exists(cacheFilePath)) {
      // Attempt to load the atlas from the cache.
      // TODO: Add some mechanism which discards the cache in case the sprite file has changed
      loaded = atlas.Load(cacheFilePath.c_str(), sprite->NumFrames());
    }
    
    if (!loaded) {
      int chosenWidth = -1;
      int chosenHeight = -1;
      if (sprite->NumFrames() == 1) {
        // Special case for a single frame: Use the sprite size (plus the border) directly as texture size.
        Sprite::Frame::Layer& layer = ((graphicOrShadow == 0) ? sprite->frame(0).graphic : sprite->frame(0).shadow);
        chosenWidth = layer.image.width() + 2 * pixelBorder;
        chosenHeight = layer.image.height() + 2 * pixelBorder;
      } else {
        int textureSize = 2048;
        int largestTooSmallSize = -1;
        int smallestAcceptableSize = -1;
        for (int attempt = 0; attempt < 8; ++ attempt) {
          if (!atlas.BuildAtlas(textureSize, textureSize, pixelBorder)) {
            // The size is too small.
            // LOG(INFO) << "Size " << textureSize << " is too small.";
            largestTooSmallSize = textureSize;
            if (smallestAcceptableSize >= 0) {
              textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
            } else {
              textureSize = 2 * largestTooSmallSize;
            }
          } else {
            // The size is large enough.
            // LOG(INFO) << "Size " << textureSize << " is okay.";
            smallestAcceptableSize = textureSize;
            if (smallestAcceptableSize >= 0) {
              textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
            } else {
              textureSize = smallestAcceptableSize / 2;
            }
          }
        }
        chosenWidth = smallestAcceptableSize;
        chosenHeight = smallestAcceptableSize;
      }
      if (chosenWidth <= 0 || chosenHeight <= 0) {
        LOG(ERROR) << "Unable to find a texture size which all animation frames can be packed into.";
        return false;
      }
      LOG(INFO) << "Atlas for " << path << " uses size: " << chosenWidth << " x " << chosenHeight;
      
      if (!atlas.BuildAtlas(chosenWidth, chosenHeight, pixelBorder)) {
        LOG(ERROR) << "Unexpected error while building an atlas image (1).";
        return false;
      }
    }
    
    QImage atlasImage = atlas.RenderAtlas();
    if (atlasImage.isNull()) {
      LOG(ERROR) << "Unexpected error while building an atlas image (2).";
      return false;
    }
    if (!loaded) {
      if (!atlas.Save(cacheFilePath.c_str())) {
        LOG(WARNING) << "Failed to save atlas cache file: " << cacheFilePath;
      }
    }
    
    // Transfer the atlasImage to the GPU
    texture->Load(atlasImage, wrapMode, (graphicOrShadow == 0) ? GL_NEAREST : GL_LINEAR, (graphicOrShadow == 0) ? GL_NEAREST : GL_LINEAR);
  }
  
  return true;
}

void DrawSprite(
    const Sprite& sprite,
    Texture& texture,
    SpriteShader* spriteShader,
    const QPointF& centerProjectedCoord,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    int frameNumber,
    bool shadow,
    bool outline,
    QRgb outlineOrModulationColor,
    int playerIndex,
    float scaling) {
  const Sprite::Frame::Layer& layer = shadow ? sprite.frame(frameNumber).shadow : sprite.frame(frameNumber).graphic;
  
  bool isGraphic = !shadow && !outline;
  int positiveOffset = isGraphic ? 1 : 0;
  int negativeOffset = isGraphic ? -1 : 0;
  
  // if (layer.rotated) {
  //   // TODO: Is this worth implementing? It will complicate the shader a little.
  // }
  
  texture.DrawCallBuffer().resize(texture.DrawCallBuffer().size() + spriteShader->GetVertexSize());
  float* data = reinterpret_cast<float*>(texture.DrawCallBuffer().data() + texture.DrawCallBuffer().size() - spriteShader->GetVertexSize());
  
  // in_position
  constexpr float kOffScreenDepthBufferExtent = 1000;
  data[0] = static_cast<float>(centerProjectedCoord.x() + scaling * (-layer.centerX + positiveOffset));
  data[1] = static_cast<float>(centerProjectedCoord.y() + scaling * (-layer.centerY + positiveOffset));
  data[2] = static_cast<float>(1.f - 2.f * (kOffScreenDepthBufferExtent + viewMatrix[0] * centerProjectedCoord.y() + viewMatrix[2]) / (2.f * kOffScreenDepthBufferExtent + widgetHeight));
  // in_size
  data[3] = scaling * zoom * 2.f * (layer.imageWidth + 2 * negativeOffset) / static_cast<float>(widgetWidth);
  data[4] = scaling * zoom * 2.f * (layer.imageHeight + 2 * negativeOffset) / static_cast<float>(widgetHeight);
  // in_tex_topleft
  data[5] = (layer.atlasX + positiveOffset) / (1.f * texture.GetWidth());
  data[6] = (layer.atlasY + positiveOffset) / (1.f * texture.GetHeight());
  // in_tex_bottomright
  data[7] = (layer.atlasX + layer.imageWidth + negativeOffset) / (1.f * texture.GetWidth());
  data[8] = (layer.atlasY + layer.imageHeight + negativeOffset) / (1.f * texture.GetHeight());
  // outline: in_playerColor; !outline && !shadow: in_modulationColor; shadow: unused
  if (!shadow) {
    u8* u8Data = reinterpret_cast<u8*>(data + 9);
    *u8Data++ = qRed(outlineOrModulationColor);
    *u8Data++ = qGreen(outlineOrModulationColor);
    *u8Data++ = qBlue(outlineOrModulationColor);
    if (!outline) {
      *u8Data++ = playerIndex;
    }
  }
}
