#include "FreeAge/map.h"

#include <mango/image/image.hpp>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"

// TODO: Make this configurable
constexpr int kTileProjectedWidth = 96;
constexpr int kTileProjectedHeight = kTileProjectedWidth / 2;
// TODO: Might want to make this smaller than in the original game to give a better overview.
//       With the default, tile occupancy on hill sides can be very hard to see.
constexpr int kTileProjectedElevationDifference = kTileProjectedHeight / 2;

Map::Map(int width, int height)
    : width(width),
      height(height) {
  maxElevation = 7;  // TODO: Make configurable
  elevation = new int[(width + 1) * (height + 1)];
  
  // Initialize the elevation to zero everywhere.
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      elevationAt(x, y) = 0;
    }
  }
}

Map::~Map() {
  delete[] elevation;
}

QPointF Map::TileCornerToProjectedCoord(int cornerX, int cornerY) {
  if (cornerX < 0 || cornerY < 0 ||
      cornerX > width || cornerY > height) {
    LOG(ERROR) << "Parameters are out-of-bounds: (" << cornerX << ", " << cornerY << ")";
    return QPointF(0, 0);
  }
  
  return cornerX * QPointF(kTileProjectedWidth / 2, kTileProjectedHeight / 2) +
         cornerY * QPointF(kTileProjectedWidth / 2, -kTileProjectedHeight / 2) +
         elevationAt(cornerX, cornerY) * QPointF(0, -kTileProjectedElevationDifference);
}

QPointF Map::MapCoordToProjectedCoord(const QPointF& mapCoord) {
  int lowerX = std::max(0, std::min(width - 1, static_cast<int>(mapCoord.x())));
  int lowerY = std::max(0, std::min(height - 1, static_cast<int>(mapCoord.y())));
  
  QPointF left = TileCornerToProjectedCoord(lowerX, lowerY);
  QPointF bottom = TileCornerToProjectedCoord(lowerX + 1, lowerY);
  QPointF top = TileCornerToProjectedCoord(lowerX, lowerY + 1);
  QPointF right = TileCornerToProjectedCoord(lowerX + 1, lowerY + 1);
  
  float xDiff = mapCoord.x() - lowerX;
  float yDiff = mapCoord.y() - lowerY;
  
  return (1 - xDiff) * (1 - yDiff) * left +
         (    xDiff) * (1 - yDiff) * bottom +
         (1 - xDiff) * (    yDiff) * top +
         (    xDiff) * (    yDiff) * right;
}

bool Map::ProjectedCoordToMapCoord(const QPointF& /*projectedCoord*/, QPointF* /*mapCoord*/) {
  // TODO
  return false;
}


void Map::GenerateRandomMap() {
  constexpr int numHills = 40;  // TODO: Make configurable
  for (int hill = 0; hill < numHills; ++ hill) {
    int tileX = rand() % width;
    int tileY = rand() % height;
    int elevationValue = rand() % maxElevation;
    PlaceElevation(tileX, tileY, elevationValue);
  }
}

void Map::PlaceElevation(int tileX, int tileY, int elevationValue) {
  int currentMinElev = elevationValue;
  int currentMaxElev = elevationValue;
  
  int minX = tileX;
  int minY = tileY;
  int maxX = tileX + 1;
  int maxY = tileY + 1;
  
  while (true) {
    // Set the current ring.
    bool anyChangeDone = false;
    
    for (int x = std::max(0, minX); x <= std::min(width, maxX); ++ x) {
      if (minY >= 0) {
        int& elev = elevationAt(x, minY);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
      if (maxY <= height) {
        int& elev = elevationAt(x, maxY);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
    }
    for (int y = std::max(0, minY + 1); y <= std::min(height, maxY - 1); ++ y) {
      if (minX >= 0) {
        int& elev = elevationAt(minX, y);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
      if (maxX <= width) {
        int& elev = elevationAt(maxX, y);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
    }
    
    if (!anyChangeDone) {
      break;
    }
    
    // Go to the next ring.
    -- currentMinElev;
    ++ currentMaxElev;
    if (currentMinElev <= 0 && currentMaxElev >= maxElevation) {
      break;
    }
    
    -- minX;
    -- minY;
    ++ maxX;
    ++ maxY;
  }
}


void Map::LoadRenderResources() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Load texture
  const char* texturePath = "/home/thomas/.local/share/Steam/steamapps/compatdata/813780/pfx/drive_c/users/steamuser/Games/Age of Empires 2 DE/76561197995377131/mods/subscribed/812_Zetnus Improved Grid Mod/resources/_common/terrain/textures/2x/g_gr2.dds";
  mango::Bitmap textureBitmap(texturePath, mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // TODO
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // TODO
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      textureBitmap.width, textureBitmap.height,
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      textureBitmap.address<u32>(0, 0));
  
  CHECK_OPENGL_NO_ERROR();
  
  // Build geometry buffer
  int elementSizeInBytes = 4 * sizeof(float);
  u8* data = new u8[(width + 1) * (height + 1) * elementSizeInBytes];
  u8* ptr = data;
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      QPointF projectedCoord = TileCornerToProjectedCoord(x, y);
      *reinterpret_cast<float*>(ptr) = projectedCoord.x();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = projectedCoord.y();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * x;
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * y;
      ptr += sizeof(float);
    }
  }
  f->glGenBuffers(1, &vertexBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  f->glBufferData(GL_ARRAY_BUFFER, (width + 1) * (height + 1) * elementSizeInBytes, data, GL_STATIC_DRAW);
  delete[] data;
  CHECK_OPENGL_NO_ERROR();
  
  // Build index buffer
  u32* indexData = new u32[width * height * 6];
  u32* indexPtr = indexData;
  for (int y = 0; y < height; ++ y) {
    for (int x = 0; x < width; ++ x) {
      int diff1 = std::abs(elevationAt(x, y) - elevationAt(x + 1, y + 1));
      int diff2 = std::abs(elevationAt(x + 1, y) - elevationAt(x, y + 1));
      
      if (diff1 < diff2) {
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
        
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
      } else {
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
        
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
      }
    }
  }
  f->glGenBuffers(1, &indexBuffer);
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  f->glBufferData(GL_ELEMENT_ARRAY_BUFFER, width * height * 6 * sizeof(u32), indexData, GL_STATIC_DRAW);
  delete[] indexData;
  CHECK_OPENGL_NO_ERROR();
  
  // Create shader program
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec2 in_position;\n"
      "in vec2 in_texcoord;\n"
      "uniform vec2 u_scaling;\n"
      "uniform vec2 u_translation;\n"
      "out vec2 var_texcoord;\n"
      "void main() {\n"
      // TODO: Use sensible z value? Or disable z writing while rendering the terrain anyway
      "  gl_Position = vec4(u_scaling.x * in_position.x + u_translation.x, u_scaling.y * in_position.y + u_translation.y, 0.999, 1);\n"
      "  var_texcoord = in_texcoord;\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 var_texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "\n"
      "void main() {\n"
      "  out_color = texture(u_texture, var_texcoord.xy);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader));
  
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  program_u_texture_location = program->GetUniformLocationOrAbort("u_texture");
  program_u_scaling_location = program->GetUniformLocationOrAbort("u_scaling");
  program_u_translation_location = program->GetUniformLocationOrAbort("u_translation");
  
  // TODO: Un-load the render resources again on destruction
}

void Map::Render(int screenWidth, int screenHeight) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program->UseProgram();
  
  // f->glEnable(GL_BLEND);
  // f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  program->SetUniform1i(program_u_texture_location, 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  program->SetUniform2f(program_u_scaling_location, 2.f / screenWidth, -2.f / screenHeight);
  program->SetUniform2f(program_u_translation_location, -1.f, 1.f - 1.f);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  program->SetPositionAttribute(
      2,
      GetGLType<float>::value,
      4 * sizeof(float),
      0);
  
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  program->SetTexCoordAttribute(
      2,
      GetGLType<float>::value,
      4 * sizeof(float),
      2 * sizeof(float));
  
  f->glDrawElements(GL_TRIANGLES, width * height * 6, GL_UNSIGNED_INT, 0);
  
  CHECK_OPENGL_NO_ERROR();
}
