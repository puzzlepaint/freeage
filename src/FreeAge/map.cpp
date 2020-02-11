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
  
  occupied = new bool[width * height];
  
  // Initialize the elevation to zero everywhere, and the occupancy to free.
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      elevationAt(x, y) = 0;
      if (x < width && y < height) {
        occupiedAt(x, y) = false;
      }
    }
  }
}

Map::~Map() {
  delete[] elevation;
  delete[] occupied;
}

QPointF Map::TileCornerToProjectedCoord(int cornerX, int cornerY) const {
  if (cornerX < 0 || cornerY < 0 ||
      cornerX > width || cornerY > height) {
    LOG(ERROR) << "Parameters are out-of-bounds: (" << cornerX << ", " << cornerY << ")";
    return QPointF(0, 0);
  }
  
  return cornerX * QPointF(kTileProjectedWidth / 2, kTileProjectedHeight / 2) +
         cornerY * QPointF(kTileProjectedWidth / 2, -kTileProjectedHeight / 2) +
         elevationAt(cornerX, cornerY) * QPointF(0, -kTileProjectedElevationDifference);
}

QPointF Map::MapCoordToProjectedCoord(const QPointF& mapCoord, QPointF* jacobianColumn0, QPointF* jacobianColumn1) const {
  int lowerX = std::max(0, std::min(width - 1, static_cast<int>(mapCoord.x())));
  int lowerY = std::max(0, std::min(height - 1, static_cast<int>(mapCoord.y())));
  
  QPointF left = TileCornerToProjectedCoord(lowerX, lowerY);
  QPointF bottom = TileCornerToProjectedCoord(lowerX + 1, lowerY);
  QPointF top = TileCornerToProjectedCoord(lowerX, lowerY + 1);
  QPointF right = TileCornerToProjectedCoord(lowerX + 1, lowerY + 1);
  
  float xDiff = mapCoord.x() - lowerX;
  float yDiff = mapCoord.y() - lowerY;
  
  if (jacobianColumn0) {
    // Left column of jacobian
    *jacobianColumn0 = - (1 - yDiff) * left +
                         (1 - yDiff) * bottom +
                       - (    yDiff) * top +
                         (    yDiff) * right;
  }
  if (jacobianColumn1) {
    // Right column of jacobian
    *jacobianColumn1 = - (1 - xDiff) * left +
                       - (    xDiff) * bottom +
                         (1 - xDiff) * top +
                         (    xDiff) * right;
  }
  
  return (1 - xDiff) * (1 - yDiff) * left +
         (    xDiff) * (1 - yDiff) * bottom +
         (1 - xDiff) * (    yDiff) * top +
         (    xDiff) * (    yDiff) * right;
}

bool Map::ProjectedCoordToMapCoord(const QPointF& projectedCoord, QPointF* mapCoord) const {
  // This is a bit more difficult than MapCoordToProjectedCoord() since we do not know the
  // elevation beforehand. Thus, we use the following strategy: Assume that the elevation is
  // constant, compute the map coord under this assumption, then go up or
  // down until we hit the actual map coord.
  int assumedElevation = maxElevation / 2;
  
  // Get the map coordinates that would result in projectedCoord given that the map was
  // flat, with an elevation of assumedElevation everywhere.
  // To do this, we solve this for x and y:
  //   originTileAtAssumedElevCoord + x * plusXDirection + y * plusYDirection = projectedCoord
  // As a matrix equation "A * x = b", this reads:
  //   (plusXDirection.x plusYDirection.x) * (x) = (projectedCoord.x - originTileAtAssumedElevCoord.x)
  //   (plusXDirection.y plusYDirection.y)   (y)   (projectedCoord.y - originTileAtAssumedElevCoord.y)
  QPointF originTileAtAssumedElevCoord =
      assumedElevation * QPointF(0, -kTileProjectedElevationDifference);
  QPointF plusXDirection = QPointF(kTileProjectedWidth / 2, kTileProjectedHeight / 2);
  QPointF plusYDirection = QPointF(kTileProjectedWidth / 2, -kTileProjectedHeight / 2);
  
  // Build matrix A.
  float A00 = plusXDirection.x();
  float A01 = plusYDirection.x();
  float A10 = plusXDirection.y();
  float A11 = plusYDirection.y();
  
  // Invert A.
  float detH = A00 * A11 - A01 * A10;
  float detH_inv = 1.f / detH;
  float A00_inv = detH_inv * A11;
  float A01_inv = detH_inv * -A01;
  float A10_inv = detH_inv * -A10;
  float A11_inv = detH_inv * A00;
  
  // Build vector b.
  float b0 = projectedCoord.x() - originTileAtAssumedElevCoord.x();
  float b1 = projectedCoord.y() - originTileAtAssumedElevCoord.y();
  
  // Compute the solution.
  *mapCoord = QPointF(
      A00_inv * b0 + A01_inv * b1,
      A10_inv * b0 + A11_inv * b1);
  
  // Clamp the initial map coordinate to be within the map
  // (while keeping the projected x coordinate constant).
  constexpr float kClampMargin = 0.001f;
  if (mapCoord->x() < 0) {
    // The coordinate is beyond the top-left map border. Move the coordinate down (in projected coordinates).
    *mapCoord += -mapCoord->x() * QPointF(1, -1);
  }
  if (mapCoord->y() >= height) {
    // The coordinate is beyond the top-right map border. Move the coordinate down (in projected coordinates).
    *mapCoord += -(mapCoord->y() - height + kClampMargin) * QPointF(1, -1);
  }
  if (mapCoord->y() < 0) {
    // The coordinate is beyond the bottom-left map border. Move the coordinate up (in projected coordinates)
    *mapCoord += -mapCoord->y() * QPointF(-1, 1);
  }
  if (mapCoord->x() >= width) {
    // The coordinate is beyond the bottom-right map border. Move the coordinate up (in projected coordinates)
    *mapCoord += -(mapCoord->x() - width + kClampMargin) * QPointF(-1, 1);
  }
  
  // We use Gauss-Newton optimization (with coordinates clamped to the map) to do the search.
  // Note that we allow both coordinates to vary here, rather than constraining the movement
  // to be vertical, since this is easily possible, the performance difference should be completely
  // negligible, and it gives us a slightly more general implementation.
  // TODO: Use Levenberg-Marquardt as it is safer
  bool converged = false;
  constexpr int kMaxNumIterations = 50;
  for (int iteration = 0; iteration < kMaxNumIterations; ++ iteration) {
    QPointF jacCol0;
    QPointF jacCol1;
    QPointF currentProjectedCoord = MapCoordToProjectedCoord(*mapCoord, &jacCol0, &jacCol1);
    QPointF residual = currentProjectedCoord - projectedCoord;
    if (residual.manhattanLength() < 1e-5f) {
      converged = true;
      break;
    }
    
    // Compute Gauss-Newton update: - H^(-1) b
    float H00 = jacCol0.x() * jacCol0.x() + jacCol0.y() * jacCol0.y();
    float H01 = jacCol0.x() * jacCol1.x() + jacCol0.y() * jacCol1.y();  // = H10
    float H11 = jacCol1.x() * jacCol1.x() + jacCol1.y() * jacCol1.y();
    
    float detH = H00 * H11 - H01 * H01;
    float detH_inv = 1.f / detH;
    float H00_inv = detH_inv * H11;
    float H01_inv = detH_inv * -H01;  // = H10_inv
    float H11_inv = detH_inv * H00;
    
    float b0 = - jacCol0.x() * residual.x() - jacCol0.y() * residual.y();
    float b1 = - jacCol1.x() * residual.x() - jacCol1.y() * residual.y();
    
    float update0 = H00_inv * b0 + H01_inv * b1;
    float update1 = H01_inv * b0 + H11_inv * b1;
    *mapCoord += QPointF(update0, update1);
    // Clamp to map area.
    *mapCoord = QPointF(
        std::max<float>(0, std::min<float>(width - kClampMargin, mapCoord->x())),
        std::max<float>(0, std::min<float>(height - kClampMargin, mapCoord->y())));
    
    // LOG(INFO) << "- currentMapCoordEstimate: " << mapCoord->x() << ", " << mapCoord->y();
  }
  return converged;
}


void Map::GenerateRandomMap() {
  // Generate town centers
  // TODO: Currently we randomly determine the leftmost tile; use the center instead for even distribution
  QPoint townCenterLocations[2];
  townCenterLocations[0] = QPoint(1 * width / 4 + (rand() % (width / 8)),
                                  1 * width / 4 + (rand() % (width / 8)));
  AddBuilding(BuildingType::TownCenter, townCenterLocations[0].x(), townCenterLocations[0].y());
  townCenterLocations[1] = QPoint(3 * width / 4 + (rand() % (width / 8)),
                                  3 * width / 4 + (rand() % (width / 8)));
  AddBuilding(BuildingType::TownCenter, townCenterLocations[1].x(), townCenterLocations[1].y());
  
  QPointF townCenterCenters[2];
  for (int player = 0; player < 2; ++ player) {
    townCenterCenters[player] = QPointF(
        townCenterLocations[player].x() + 0.5f * GetBuildingSize(BuildingType::TownCenter).width(),
        townCenterLocations[player].y() + 0.5f * GetBuildingSize(BuildingType::TownCenter).height());
  }
  
  auto getRandomLocation = [&](float minDistanceToTCs, int* tileX, int* tileY) {
    bool ok;
    for (int attempt = 0; attempt < 1000; ++ attempt) {
      *tileX = rand() % width;
      *tileY = rand() % height;
      ok = true;
      for (int i = 0; i < 2; ++ i) {
        float distanceX = townCenterCenters[i].x() - *tileX;
        float distanceY = townCenterCenters[i].y() - *tileY;
        float distance = sqrtf(distanceX * distanceX + distanceY * distanceY);
        if (distance < minDistanceToTCs) {
          ok = false;
          break;
        }
      }
      if (ok) {
        break;
      }
    }
    return ok;
  };
  
  // Generate forests
  constexpr int kForestMinDistanceFromTCs = 10;  // TODO: Make configurable
  constexpr int kNumForests = 6;  // TODO: Make configurable
  for (int forest = 0; forest < kNumForests; ++ forest) {
    int tileX;
    int tileY;
    if (getRandomLocation(kForestMinDistanceFromTCs, &tileX, &tileY)) {
      // Place the forest.
      // TODO: For now, we just place very simple circles.
      int forestRadius = 6 + rand() % 3;
      
      int minX = std::max(0, tileX - forestRadius);
      int maxX = std::min(width - 1, tileX + forestRadius);
      int minY = std::max(0, tileY - forestRadius);
      int maxY = std::min(height - 1, tileY + forestRadius);
      
      for (int y = minY; y <= maxY; ++ y) {
        for (int x = minX; x <= maxX; ++ x) {
          int diffX = x - tileX;
          int diffY = y - tileY;
          float radius = sqrtf(diffX * diffX + diffY * diffY);
          if (radius <= forestRadius && !occupiedAt(x, y)) {
            AddBuilding(BuildingType::TreeOak, x, y);
          }
        }
      }
    }
  }
  
  // Generate hills
  const int kHillMinDistanceFromTCs = maxElevation + 2 + 8;  // TODO: Make configurable
  constexpr int numHills = 40;  // TODO: Make configurable
  for (int hill = 0; hill < numHills; ++ hill) {
    int tileX;
    int tileY;
    if (getRandomLocation(kHillMinDistanceFromTCs, &tileX, &tileY)) {
      int elevationValue = rand() % maxElevation;
      PlaceElevation(tileX, tileY, elevationValue);
    }
  }
  
  // Generate villagers
  for (int player = 0; player < 2; ++ player) {
    for (int villager = 0; villager < 3; ++ villager) {
      float radius = 4 + 2 * ((rand() % 10000) / 10000.f);
      float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
      QPointF spawnLoc(
          townCenterCenters[player].x() + radius * sin(angle),
          townCenterCenters[player].y() + radius * cos(angle));
      units.insert(std::make_pair(nextUnitID++, ClientUnit((rand() % 2 == 0) ? UnitType::FemaleVillager : UnitType::MaleVillager, spawnLoc)));
    }
  }
  
  // Generate scouts
  for (int player = 0; player < 2; ++ player) {
    float radius = 6 + 2 * ((rand() % 10000) / 10000.f);
    float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
    QPointF spawnLoc(
        townCenterCenters[player].x() + radius * sin(angle),
        townCenterCenters[player].y() + radius * cos(angle));
    units.insert(std::make_pair(nextUnitID++, ClientUnit(UnitType::Scout, spawnLoc)));
  }
}

void Map::AddBuilding(BuildingType type, int baseTileX, int baseTileY) {
  // Insert into buildings map
  buildings.insert(std::make_pair(nextBuildingID++, ClientBuilding(type, baseTileX, baseTileY)));
  
  // Mark the occupied tiles as such
  QSize size = GetBuildingSize(type);
  for (int y = baseTileY; y < baseTileY + size.height(); ++ y) {
    for (int x = baseTileX; x < baseTileX + size.width(); ++ x) {
      occupiedAt(x, y) = true;
    }
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
      "uniform mat2 u_viewMatrix;\n"
      "out vec2 var_texcoord;\n"
      "void main() {\n"
      // TODO: Use sensible z value? Or disable z writing while rendering the terrain anyway
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], 0.999, 1);\n"
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
  program_u_viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix");
  
  // TODO: Un-load the render resources again on destruction
}

struct BuildingToRender {
  inline BuildingToRender(ClientBuilding* building, float y)
      : building(building),
        y(y) {}
  
  inline bool operator< (const BuildingToRender& other) {
    return y < other.y;
  }
  
  ClientBuilding* building;
  float y;
};

void Map::Render(
    float* viewMatrix,
    const std::vector<ClientUnitType>& unitTypes,
    const std::vector<Sprite>& buildingSprites,
    const std::vector<Texture>& buildingTextures,
    SpriteShader* spriteShader,
    GLuint spritePointBuffer,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    float elapsedSeconds) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Render the map.
  program->UseProgram();
  
  // f->glEnable(GL_BLEND);
  // f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  program->SetUniform1i(program_u_texture_location, 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  program->setUniformMatrix2fv(program_u_viewMatrix_location, viewMatrix);
  
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
  
  // Determine the view rect in projected coordinates.
  // opengl_x = viewMatrix[0] * projected_x + viewMatrix[2];
  // opengl_y = viewMatrix[1] * projected_y + viewMatrix[3];
  // -->
  // projected_x = (opengl_x - viewMatrix[2]) / viewMatrix[0]
  // projected_y = (opengl_y - viewMatrix[3]) / viewMatrix[1];
  float leftProjectedCoords = ((-1) - viewMatrix[2]) / viewMatrix[0];
  float rightProjectedCoords = ((1) - viewMatrix[2]) / viewMatrix[0];
  float topProjectedCoords = ((1) - viewMatrix[3]) / viewMatrix[1];
  float bottomProjectedCoords = ((-1) - viewMatrix[3]) / viewMatrix[1];
  QRectF projectedCoordsViewRect(
      leftProjectedCoords,
      topProjectedCoords,
      rightProjectedCoords - leftProjectedCoords,
      bottomProjectedCoords - topProjectedCoords);
  
  // Render the buildings.
  std::vector<BuildingToRender> buildingsToRender;
  buildingsToRender.reserve(256);
  for (auto& buildingEntry : buildings) {
    ClientBuilding& building = buildingEntry.second;
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        this,
        buildingSprites,
        elapsedSeconds);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      // TODO: Currently, the bottom y of the sprite is used for sorting. Use the center point instead?
      // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
      buildingsToRender.push_back(BuildingToRender(&building, projectedCoordsRect.bottom()));
    }
  }
  
  // Sort by y-position
  std::sort(buildingsToRender.begin(), buildingsToRender.end());
  
  for (const auto& item : buildingsToRender) {
    ClientBuilding& building = *item.building;
    building.Render(
        this,
        buildingSprites,
        buildingTextures,
        spriteShader,
        spritePointBuffer,
        zoom,
        widgetWidth,
        widgetHeight,
        elapsedSeconds);
  }
  
  // Render the units.
  // TODO: Sort to render in correct order (or use Z-buffer).
  // TODO: If behind a building and the sprite has an outline, render the outline instead.
  for (auto& item : units) {
    item.second.Render(
        this,
        unitTypes,
        spriteShader,
        spritePointBuffer,
        zoom,
        widgetWidth,
        widgetHeight,
        elapsedSeconds);
  }
}
