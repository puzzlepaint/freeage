#include "FreeAge/sprite.h"

#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"
#include "FreeAge/shader_program.h"
#include "FreeAge/shader_sprite.h"
#include "FreeAge/sprite_atlas.h"
#include "FreeAge/texture.h"

QImage LoadSMXGraphicLayer(
    const SMXLayerHeader& layerHeader,
    const std::vector<SMPLayerRowEdge>& rowEdges,
    bool usesEightToFiveCompression,
    const Palette& standardPalette,
    const Palette& playerColorPalette,
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
        bool ignoreAlpha = true;  /*layerType == SMXLayerType::Graphic*/
        for (int i = 0; i < count; ++ i) {
          if (usesEightToFiveCompression) {
            *out++ = DecompressNextPixel8To5(pixelPtr, decompressionState, palette, ignoreAlpha);
          } else {
            *out++ = DecompressNextPixel4Plus1(pixelPtr, decompressionState, palette, ignoreAlpha);
          }
        }
        col += count;
      } else if (commandCode == 0b11) {
        // End of row.
        if (col + edge.rightSpace != layerHeader.width) {
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
        *out++ = 255;
      }
      continue;
    }
    
    // Left edge skip
    for (int col = 0; col < edge.leftSpace; ++ col) {
      *out++ = 255;
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
          *out++ = 255;
        }
        col += count;
      } else if (commandCode == 0b01) {
        // Draw *count* pixels.
        u8 count = (command >> 2) + 1;
        for (int i = 0; i < count; ++ i) {
          *out++ = 255 - *dataPtr++;
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
          *out++ = 255;
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
    const Palette& playerColorPalette,
    SMXLayerType layerType,
    Sprite::Frame::Layer* layer,
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
  
  layer->centerX = layerHeader.hotspotX;
  layer->centerY = layerHeader.hotspotY;
  
  // Read the row edge data.
  std::vector<SMPLayerRowEdge> rowEdges(layerHeader.height);
  for (int row = 0; row < layerHeader.height; ++ row) {
    if (fread(&rowEdges[row], sizeof(SMPLayerRowEdge), 1, file) != 1) {
      LOG(ERROR) << "Unexpected EOF while trying to read SMPLayerRowEdge for row " << row;
      return false;
    }
    // LOG(INFO) << "Row edge: L: " << rowEdges[row].leftSpace << " R: " << rowEdges[row].rightSpace;
  }
  
  if (layerType == SMXLayerType::Graphic) {
    layer->image = LoadSMXGraphicLayer(
        layerHeader,
         rowEdges,
        usesEightToFiveCompression,
        standardPalette,
        playerColorPalette,
        file);
  } else if (layerType == SMXLayerType::Shadow) {
    layer->image = LoadSMXShadowLayer(
        layerHeader,
        rowEdges,
        file);
  } else if (layerType == SMXLayerType::Outline) {
    layer->image = LoadSMXOutlineLayer(
        layerHeader,
        rowEdges,
        file);
  }
  if (layer->image.isNull()) {
    return false;
  }
  
  return true;
}


Palette LoadPalette(const std::filesystem::path& path) {
  Palette result;
  
  bool hasAlpha = path.extension().string() == ".palx";
  
  FILE* file = fopen(path.c_str(), "rb");
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

bool Sprite::LoadFromFile(const char* path, const Palettes& palettes) {
  // TODO: This only supports the .smx format at the moment. Also support .slp, for example.
  
  FILE* file = fopen(path, "rb");
  if (!file) {
    LOG(ERROR) << "Cannot open file: " << path;
    return false;
  }
  std::shared_ptr<FILE> fileCloser(file, [&](FILE* file) { fclose(file); });
  
  // Read the header and verify it.
  SMXHeader header;
  if (fread(&header, sizeof(SMXHeader), 1, file) != 1) {
    LOG(ERROR) << "Unexpected EOF while trying to read SMXHeader";
    return false;
  }
  
  if (header.fileDescriptor[0] != 'S' ||
      header.fileDescriptor[1] != 'M' ||
      header.fileDescriptor[2] != 'P' ||
      header.fileDescriptor[3] != 'X') {
    LOG(ERROR) << "Header file descriptor is not SMPX\nActual data: "
               << header.fileDescriptor[0] << header.fileDescriptor[1] << header.fileDescriptor[2] << header.fileDescriptor[3];
    return false;
  }
  
  // LOG(INFO) << "Version: " << header.version;
  // LOG(INFO) << "Frame count: " << header.numFrames;
  
  frames.resize(header.numFrames);
  for (int frameIdx = 0; frameIdx < header.numFrames; ++ frameIdx) {
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
    const Palette& playerColorPalette = palettes.at(55);  // TODO: Hardcoded the blue player palette
    
    // Read graphic layer
    if (frameHeader.HasGraphicLayer()) {
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          playerColorPalette,
          SMXLayerType::Graphic,
          &frame.graphic,
          file)) {
        return false;
      }
    }
    
    // Read shadow layer
    if (frameHeader.HasShadowLayer()) {
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          playerColorPalette,
          SMXLayerType::Shadow,
          &frame.shadow,
          file)) {
        return false;
      }
    }
    
    // Read outline layer
    if (frameHeader.HasOutlineLayer()) {
      if (!LoadSMXLayer(
          frameHeader.UsesEightToFiveCompression(),
          standardPalette,
          playerColorPalette,
          SMXLayerType::Outline,
          &frame.outline,
          file)) {
        return false;
      }
    }
  }
  
  return true;
}


bool LoadSpriteAndTexture(const char* path, int wrapMode, int magFilter, int minFilter, Sprite* sprite, Texture* texture, const Palettes& palettes) {
  if (!sprite->LoadFromFile(path, palettes)) {
    LOG(ERROR) << "Failed to load sprite from " << path;
    return false;
  }
  
  // Create a sprite atlas texture containing all frames of the SMX animation.
  // TODO: This generally takes a LOT of memory. We probably want to do a dense packing of the images using
  //       non-rectangular geometry to save some more space.
  SpriteAtlas atlas;
  atlas.AddSprite(sprite);
  
  constexpr int pixelBorder = 1;
  
  int chosenWidth = -1;
  int chosenHeight = -1;
  if (sprite->NumFrames() == 1) {
    // Special case for a single frame: Use the sprite size (plus the border) directly as texture size.
    chosenWidth = sprite->frame(0).graphic.image.width() + 2 * pixelBorder;
    chosenHeight = sprite->frame(0).graphic.image.height() + 2 * pixelBorder;
  } else {
    int textureSize = 2048;
    int largestTooSmallSize = -1;
    int smallestAcceptableSize = -1;
    for (int attempt = 0; attempt < 8; ++ attempt) {
      if (!atlas.BuildAtlas(textureSize, textureSize, nullptr, pixelBorder)) {
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
  QImage atlasImage;
  if (!atlas.BuildAtlas(chosenWidth, chosenHeight, &atlasImage, pixelBorder)) {
    LOG(ERROR) << "Unexpected error while building an atlas image.";
    return false;
  }
  
  // Transfer the atlasImage to the GPU
  texture->Load(atlasImage, wrapMode, magFilter, minFilter);
  
  return true;
}

void DrawSprite(
    const Sprite& sprite,
    const Texture& texture,
    SpriteShader* spriteShader,
    const QPointF& centerProjectedCoord,
    GLuint pointBuffer,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    int frameNumber) {
  const Sprite::Frame::Layer& layer = sprite.frame(frameNumber).graphic;
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  ShaderProgram* program = spriteShader->GetProgram();
  program->UseProgram();
  program->SetUniform1i(spriteShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, texture.GetId());
  
  program->SetUniform2f(spriteShader->GetSizeLocation(), zoom * 2.f * layer.image.width() / static_cast<float>(widgetWidth), zoom * 2.f * layer.image.height() / static_cast<float>(widgetHeight));
  float texLeftX = layer.atlasX / (1.f * texture.GetWidth());
  float texTopY = layer.atlasY / (1.f * texture.GetHeight());
  float texRightX = (layer.atlasX + layer.image.width()) / (1.f * texture.GetWidth());
  float texBottomY = (layer.atlasY + layer.image.height()) / (1.f * texture.GetHeight());
  if (layer.rotated) {
    // TODO: Is this worth implementing? It will complicate the shader a little.
  }
  program->SetUniform2f(spriteShader->GetTextTopLeftLocation(), texLeftX, texTopY);
  program->SetUniform2f(spriteShader->GetTexBottomRightLocation(), texRightX, texBottomY);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  float data[] = {
      static_cast<float>(centerProjectedCoord.x() - layer.centerX),
      static_cast<float>(centerProjectedCoord.y() - layer.centerY),
      0};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0);
  
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}
