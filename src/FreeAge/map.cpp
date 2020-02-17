#include "FreeAge/map.h"

#include <mango/image/image.hpp>

#include "FreeAge/free_age.h"
#include "FreeAge/health_bar.h"
#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"

// TODO: Make this configurable
constexpr int kTileProjectedWidth = 96;
constexpr int kTileProjectedHeight = kTileProjectedWidth / 2;
// TODO: Might want to make this smaller than in the original game to give a better overview.
//       With the default, tile occupancy on hill sides can be very hard to see.
constexpr int kTileProjectedElevationDifference = kTileProjectedHeight / 2;

const float kTileDiagonalLength = 0.5f * sqrtf(kTileProjectedWidth * kTileProjectedWidth + kTileProjectedHeight * kTileProjectedHeight);

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
  // TODO: This did not work in all cases, there are cases where the result ends up outside of the map.
  // if (mapCoord->x() < 0) {
  //   // The coordinate is beyond the top-left map border. Move the coordinate down (in projected coordinates).
  //   *mapCoord += -mapCoord->x() * QPointF(1, -1);
  // }
  // if (mapCoord->y() >= height) {
  //   // The coordinate is beyond the top-right map border. Move the coordinate down (in projected coordinates).
  //   *mapCoord += -(mapCoord->y() - height + kClampMargin) * QPointF(1, -1);
  // }
  // if (mapCoord->y() < 0) {
  //   // The coordinate is beyond the bottom-left map border. Move the coordinate up (in projected coordinates)
  //   *mapCoord += -mapCoord->y() * QPointF(-1, 1);
  // }
  // if (mapCoord->x() >= width) {
  //   // The coordinate is beyond the bottom-right map border. Move the coordinate up (in projected coordinates)
  //   *mapCoord += -(mapCoord->x() - width + kClampMargin) * QPointF(-1, 1);
  // }
  
  // Safer approach: more straightforward clamping. The coordinate might not end up at the same projected x-coordinate,
  // but that does not concern us.
  *mapCoord = QPointF(
      std::max<double>(0, std::min<double>(width - kClampMargin, mapCoord->x())),
      std::max<double>(0, std::min<double>(height - kClampMargin, mapCoord->y())));
  
  // We use Gauss-Newton optimization (with coordinates clamped to the map) to do the search.
  // Note that we allow both coordinates to vary here, rather than constraining the movement
  // to be vertical, since this is easily possible, the performance difference should be completely
  // negligible, and it gives us a slightly more general implementation.
  bool converged = false;
  constexpr int kMaxNumIterations = 50;
  double lambda = 0;
  for (int iteration = 0; iteration < kMaxNumIterations; ++ iteration) {
    QPointF jacCol0;
    QPointF jacCol1;
    QPointF currentProjectedCoord = MapCoordToProjectedCoord(*mapCoord, &jacCol0, &jacCol1);
    QPointF residual = currentProjectedCoord - projectedCoord;
    double cost = residual.x() * residual.x() + residual.y() * residual.y();
    if (cost < 1e-8f) {
      converged = true;
      break;
    }
    
    bool foundAnUpdate = false;
    for (int lmIteration = 0; lmIteration < 8; ++ lmIteration) {
      // Compute update: - (H + lambda I)^(-1) b
      double H00 = jacCol0.x() * jacCol0.x() + jacCol0.y() * jacCol0.y() + lambda;
      double H01 = jacCol0.x() * jacCol1.x() + jacCol0.y() * jacCol1.y();  // = H10
      double H11 = jacCol1.x() * jacCol1.x() + jacCol1.y() * jacCol1.y() + lambda;
      
      double detH = H00 * H11 - H01 * H01;
      double detH_inv = 1.f / detH;
      double H00_inv = detH_inv * H11;
      double H01_inv = detH_inv * -H01;  // = H10_inv
      double H11_inv = detH_inv * H00;
      
      double b0 = - jacCol0.x() * residual.x() - jacCol0.y() * residual.y();
      double b1 = - jacCol1.x() * residual.x() - jacCol1.y() * residual.y();
      
      QPointF testMapCoord(
          mapCoord->x() + H00_inv * b0 + H01_inv * b1,
          mapCoord->y() + H01_inv * b0 + H11_inv * b1);
      // Clamp to map area.
      testMapCoord = QPointF(
          std::max<double>(0, std::min<double>(width - kClampMargin, testMapCoord.x())),
          std::max<double>(0, std::min<double>(height - kClampMargin, testMapCoord.y())));
      
      // Check if the update made progress.
      QPointF currentProjectedCoord = MapCoordToProjectedCoord(testMapCoord);
      QPointF testResidual = currentProjectedCoord - projectedCoord;
      double testCost = testResidual.x() * testResidual.x() + testResidual.y() * testResidual.y();
      if (testCost < cost) {
        *mapCoord = testMapCoord;
        lambda *= 0.5;
        foundAnUpdate = true;
        break;
      } else {
        if (lambda == 0) {
          lambda = 0.01f * 0.5f * (H00 + H11);
        } else {
          lambda *= 2;
        }
      }
    }
    
    if (!foundAnUpdate) {
      break;
    }
    
    // LOG(INFO) << "- currentMapCoordEstimate: " << mapCoord->x() << ", " << mapCoord->y();
  }
  // Debug output:
  // if (!converged) {
  //   QPointF currentProjectedCoord = MapCoordToProjectedCoord(*mapCoord);
  //   QPointF residual = currentProjectedCoord - projectedCoord;
  //   LOG(WARNING) << "NOT CONVERGED; residual.manhattanLength(): " << residual.manhattanLength();
  // }
  return converged;
}


void Map::GenerateRandomMap(const std::vector<ClientBuildingType>& buildingTypes) {
  // Generate town centers
  // TODO: Currently we randomly determine the leftmost tile; use the center instead for even distribution
  QPoint townCenterLocations[2];
  townCenterLocations[0] = QPoint(1 * width / 4 + (rand() % (width / 8)),
                                  1 * width / 4 + (rand() % (width / 8)));
  AddBuilding(0, BuildingType::TownCenter, townCenterLocations[0].x(), townCenterLocations[0].y(), buildingTypes);
  townCenterLocations[1] = QPoint(3 * width / 4 + (rand() % (width / 8)),
                                  3 * width / 4 + (rand() % (width / 8)));
  AddBuilding(1, BuildingType::TownCenter, townCenterLocations[1].x(), townCenterLocations[1].y(), buildingTypes);
  
  townCenterCenters.resize(2);
  for (int player = 0; player < 2; ++ player) {
    townCenterCenters[player] = QPointF(
        townCenterLocations[player].x() + 0.5f * buildingTypes[static_cast<int>(BuildingType::TownCenter)].GetSize().width(),
        townCenterLocations[player].y() + 0.5f * buildingTypes[static_cast<int>(BuildingType::TownCenter)].GetSize().height());
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
  constexpr float kForestMinDistanceFromOtherForests = 10;
  constexpr int kNumForests = 11;  // TODO: Make configurable
  QPointF forestCenters[kNumForests];
  for (int forest = 0; forest < kNumForests; ++ forest) {
    int tileX;
    int tileY;
retryForest:  // TODO: Ugly implementation, improve this
    if (getRandomLocation(kForestMinDistanceFromTCs, &tileX, &tileY)) {
      for (int otherForest = 0; otherForest < forest; ++ otherForest) {
        float distanceX = forestCenters[otherForest].x() - tileX;
        float distanceY = forestCenters[otherForest].y() - tileY;
        float distance = sqrtf(distanceX * distanceX + distanceY * distanceY);
        if (distance < kForestMinDistanceFromOtherForests) {
          goto retryForest;
        }
      }
      
      forestCenters[forest] = QPointF(tileX + 0.5f, tileY + 0.5f);
      
      // Place the forest.
      // TODO: For now, we just place very simple circles.
      int forestRadius = 4 + rand() % 3;
      
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
            AddBuilding(-1, BuildingType::TreeOak, x, y, buildingTypes);
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
      while (true) {
        // TODO: Account for collisions with other units too.
        // TODO: Prevent this from potentially being an endless loop
        float radius = 4 + 2 * ((rand() % 10000) / 10000.f);
        float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
        QPointF spawnLoc(
            townCenterCenters[player].x() + radius * sin(angle),
            townCenterCenters[player].y() + radius * cos(angle));
        int spawnLocTileX = static_cast<int>(spawnLoc.x());
        int spawnLocTileY = static_cast<int>(spawnLoc.y());
        if (spawnLocTileX < 0 || spawnLocTileY < 0 ||
            spawnLocTileX >= width || spawnLocTileY >= height ||
            !occupiedAt(spawnLocTileX, spawnLocTileY)) {
          units.insert(std::make_pair(nextObjectID++, ClientUnit(player, (rand() % 2 == 0) ? UnitType::FemaleVillager : UnitType::MaleVillager, spawnLoc)));
          break;
        }
      }
    }
  }
  
  // Generate scouts
  for (int player = 0; player < 2; ++ player) {
    while (true) {
      // TODO: Account for collisions with other units too.
      // TODO: Prevent this from potentially being an endless loop
      float radius = 6 + 2 * ((rand() % 10000) / 10000.f);
      float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
      QPointF spawnLoc(
          townCenterCenters[player].x() + radius * sin(angle),
          townCenterCenters[player].y() + radius * cos(angle));
      int spawnLocTileX = static_cast<int>(spawnLoc.x());
      int spawnLocTileY = static_cast<int>(spawnLoc.y());
      if (spawnLocTileX < 0 || spawnLocTileY < 0 ||
          spawnLocTileX >= width || spawnLocTileY >= height ||
          !occupiedAt(spawnLocTileX, spawnLocTileY)) {
        units.insert(std::make_pair(nextObjectID++, ClientUnit(player, UnitType::Scout, spawnLoc)));
        break;
      }
    }
  }
}

void Map::AddBuilding(int player, BuildingType type, int baseTileX, int baseTileY, const std::vector<ClientBuildingType>& buildingTypes) {
  // Insert into buildings map
  buildings.insert(std::make_pair(nextObjectID++, ClientBuilding(player, type, baseTileX, baseTileY)));
  
  // Mark the occupied tiles as such
  QSize size = buildingTypes[static_cast<int>(type)].GetSize();
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
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      textureBitmap.width, textureBitmap.height,
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      textureBitmap.address<u32>(0, 0));
  f->glGenerateMipmap(GL_TEXTURE_2D);
  
  CHECK_OPENGL_NO_ERROR();
  
  // Build geometry buffer
  int elementSizeInBytes = 5 * sizeof(float);
  u8* data = new u8[(width + 1) * (height + 1) * elementSizeInBytes];
  u8* ptr = data;
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      QPointF projectedCoord = TileCornerToProjectedCoord(x, y);
      
      // Estimate the vertex normal
      // TODO: This is quite messy, it would be nice to have a proper 3D vector class for this.
      float elevationHere = elevationAt(x, y);
      float topLeftHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(std::max(0, x - 1), y) - elevationHere);
      float bottomRightHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(std::min(width - 1, x + 1), y) - elevationHere);
      float bottomLeftHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(x, std::max(0, y - 1)) - elevationHere);
      float topRightHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(x, std::min(height - 1, y + 1)) - elevationHere);
      
      float normalX = topLeftHeight - bottomRightHeight;
      float normalY = bottomLeftHeight - topRightHeight;
      
      float normalLength = sqrtf(normalX * normalX + normalY * normalY + 1 * 1);
      normalX /= normalLength;
      normalY /= normalLength;
      float normalZ = 1 / normalLength;
      
      const float lightingDirectionX = 0.3f / sqrtf(0.3f * 0.3f + 0 * 0 + 0.8f * 0.8f);
      const float lightingDirectionY = 0.f;
      const float lightingDirectionZ = 0.8f / sqrtf(0.3f * 0.3f + 0 * 0 + 0.8f * 0.8f);
      
      float dot = normalX * lightingDirectionX + normalY * lightingDirectionY + normalZ * lightingDirectionZ;
      
      // Scale such that upright terrain gets a lighting factor of one
      float lightingFactor = dot / lightingDirectionZ;
      
      // Position
      *reinterpret_cast<float*>(ptr) = projectedCoord.x();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = projectedCoord.y();
      ptr += sizeof(float);
      
      // Texture coordinate
      *reinterpret_cast<float*>(ptr) = 0.1f * x;
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * y;
      ptr += sizeof(float);
      
      // Darkening factor for map lighting
      // NOTE: This is passed on as part of the texture coordinates (for convenience)
      *reinterpret_cast<float*>(ptr) = lightingFactor;
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
      int horizontalDiff = std::abs(elevationAt(x, y) - elevationAt(x + 1, y + 1));
      int verticalDiff = std::abs(elevationAt(x + 1, y) - elevationAt(x, y + 1));
      
      // The special case was needed to make the elevation difference visible at all, since in this case,
      // the left, upper, and right vertex are all at the same y-coordinate in projected coordinates.
      bool specialCase = (horizontalDiff == 0) && ((elevationAt(x + 1, y) - elevationAt(x, y + 1)) == 1);
      if (horizontalDiff < verticalDiff && !specialCase) {
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
  
  terrainShader.reset(new TerrainShader());
  
  // TODO: Un-load the render resources again on destruction
}

void Map::Render(
    float* viewMatrix,
    const std::vector<ClientUnitType>& unitTypes,
    const std::vector<ClientBuildingType>& buildingTypes,
    const std::vector<QRgb>& playerColors,
    SpriteShader* shadowShader,
    SpriteShader* spriteShader,
    SpriteShader* outlineShader,
    HealthBarShader* healthBarShader,
    GLuint spritePointBuffer,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    double elapsedSeconds,
    int moveToFrameIndex,
    const QPointF& moveToMapCoord,
    const SpriteAndTextures& moveToSprite) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
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
  
  // NOTE: We assume that the screen has been cleared with color values (0, 0, 0, 0) before this is called.
  
  // Render the shadows.
  f->glEnable(GL_BLEND);
  f->glDisable(GL_DEPTH_TEST);
  // Set up blending such that colors are added (does not matter since we do not render colors),
  // and for alpha values, the maximum is used.
  f->glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
  
  for (auto& buildingEntry : buildings) {
    ClientBuilding& building = buildingEntry.second;
    if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasShadow()) {
      continue;
    }
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        this,
        buildingTypes,
        elapsedSeconds,
        true,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      building.Render(
          this,
          buildingTypes,  // using the shadow texture here
          playerColors,
          shadowShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          true,
          false);
    }
  }
  for (auto& item : units) {
    if (!unitTypes[static_cast<int>(item.second.GetType())].GetAnimations(item.second.GetCurrentAnimation()).front().sprite.HasShadow()) {
      continue;
    }
    
    QRectF projectedCoordsRect = item.second.GetRectInProjectedCoords(
        this,
        unitTypes,
        elapsedSeconds,
        true,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      item.second.Render(
          this,
          unitTypes,
          playerColors,
          shadowShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          true,
          false);
    }
  }
  
  // Render the map.
  f->glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);  // blend with the shadows
  
  ShaderProgram* terrainProgram = terrainShader->GetProgram();
  terrainProgram->UseProgram();
  
  terrainProgram->SetUniform1i(terrainShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  terrainProgram->setUniformMatrix2fv(terrainShader->GetViewMatrixLocation(), viewMatrix);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  terrainProgram->SetPositionAttribute(
      2,
      GetGLType<float>::value,
      5 * sizeof(float),
      0);
  
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  terrainProgram->SetTexCoordAttribute(
      3,
      GetGLType<float>::value,
      5 * sizeof(float),
      2 * sizeof(float));
  
  f->glDrawElements(GL_TRIANGLES, width * height * 6, GL_UNSIGNED_INT, 0);
  CHECK_OPENGL_NO_ERROR();
  
  f->glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);  // reset the blend func to standard
  
  // Enable the depth buffer for sprite rendering.
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  
  // Render the buildings.
  // TODO: Sort to minmize texture switches.
  for (auto& buildingEntry : buildings) {
    ClientBuilding& building = buildingEntry.second;
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        this,
        buildingTypes,
        elapsedSeconds,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
      building.Render(
          this,
          buildingTypes,
          playerColors,
          spriteShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          false);
    }
  }
  
  // Render outlines.
  // Disable depth writing.
  f->glDepthMask(GL_FALSE);
  // Let only pass through those fragments which are *behind* the depth values in the depth buffer.
  // So we only render outlines in places where something is occluded.
  f->glDepthFunc(GL_GREATER);
  
  // Render the building outlines.
  // TODO: Sort to minmize texture switches.
  // TODO: Does any building have an outline? Or can we delete this?
  // for (auto& buildingEntry : buildings) {
  //   ClientBuilding& building = buildingEntry.second;
  //   if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasOutline()) {
  //     continue;
  //   }
  //   LOG(WARNING) << "DEBUG: Buildings with outline exist!";
  //   
  //   QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
  //       this,
  //       buildingTypes,
  //       elapsedSeconds,
  //       false,
  //       true);
  //   if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
  //     // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
  //     building.Render(
  //         this,
  //         buildingTypes,
  //         playerColors,
  //         outlineShader,
  //         spritePointBuffer,
  //         viewMatrix,
  //         zoom,
  //         widgetWidth,
  //         widgetHeight,
  //         elapsedSeconds,
  //         false,
  //         true);
  //   }
  // }
  
  // Render the unit outlines.
  // TODO: Sort to minmize texture switches.
  for (auto& item : units) {
    ClientUnit& unit = item.second;
    if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front().sprite.HasOutline()) {
      continue;
    }
    
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        this,
        unitTypes,
        elapsedSeconds,
        false,
        true);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      unit.Render(
          this,
          unitTypes,
          playerColors,
          outlineShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          true);
    }
  }
  
  // Render the units.
  f->glDepthMask(GL_TRUE);
  f->glDepthFunc(GL_LEQUAL);
  
  // TODO: Sort to minmize texture switches.
  for (auto& item : units) {
    ClientUnit& unit = item.second;
    
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        this,
        unitTypes,
        elapsedSeconds,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      unit.Render(
          this,
          unitTypes,
          playerColors,
          spriteShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          false);
    }
  }
  
  // Render the move-to marker.
  // This should be rendered after the last unit at the moment, since it contains semi-transparent
  // pixels which do currenly write to the z-buffer.
  if (moveToFrameIndex >= 0) {
    QPointF projectedCoord = MapCoordToProjectedCoord(moveToMapCoord);
    DrawSprite(
        moveToSprite.sprite,
        moveToSprite.graphicTexture,
        spriteShader,
        projectedCoord,
        spritePointBuffer,
        viewMatrix,
        zoom,
        widgetWidth,
        widgetHeight,
        moveToFrameIndex,
        /*shadow*/ false,
        /*outline*/ false,
        playerColors,
        /*playerIndex*/ 0,
        /*scaling*/ 0.5f);
  }
  
  // Render health bars.
  f->glClear(GL_DEPTH_BUFFER_BIT);
  f->glDisable(GL_BLEND);
  
  QRgb gaiaColor = qRgb(255, 255, 255);
  
  // Render health bars for buildings.
  for (auto& buildingEntry : buildings) {
    // TODO: Only render the health bar if the building is selected
    
    ClientBuilding& building = buildingEntry.second;
    const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
    
    QPointF centerProjectedCoord = building.GetCenterProjectedCoord(this, buildingTypes);
    QPointF healthBarCenter =
        centerProjectedCoord +
        QPointF(0, -1 * buildingType.GetHealthBarHeightAboveCenter(building.GetFrameIndex(buildingType, elapsedSeconds)));
    
    constexpr float kHealthBarWidth = 60;  // TODO: Smaller bar for trees
    constexpr float kHealthBarHeight = 3;
    QRectF barRect(
        std::round(healthBarCenter.x() - 0.5f * kHealthBarWidth),
        std::round(healthBarCenter.y() - 0.5f * kHealthBarHeight),
        kHealthBarWidth,
        kHealthBarHeight);
    if (barRect.intersects(projectedCoordsViewRect)) {
      RenderHealthBar(
          barRect,
          centerProjectedCoord.y(),
          1.f,  // TODO: Determine fill amount based on HP
          (building.GetPlayerIndex() < 0) ? gaiaColor : playerColors[building.GetPlayerIndex()],
          healthBarShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight);
    }
  }
  
  // Render health bars for units.
  for (auto& item : units) {
    // TODO: Only render the health bar if the unit is selected
    
    ClientUnit& unit = item.second;
    const ClientUnitType& unitType = unitTypes[static_cast<int>(unit.GetType())];
    
    QPointF centerProjectedCoord = unit.GetCenterProjectedCoord(this);
    QPointF healthBarCenter =
        centerProjectedCoord +
        QPointF(0, -1 * unitType.GetHealthBarHeightAboveCenter());
    
    constexpr float kHealthBarWidth = 30;
    constexpr float kHealthBarHeight = 3;
    QRectF barRect(
        std::round(healthBarCenter.x() - 0.5f * kHealthBarWidth),
        std::round(healthBarCenter.y() - 0.5f * kHealthBarHeight),
        kHealthBarWidth,
        kHealthBarHeight);
    if (barRect.intersects(projectedCoordsViewRect)) {
      RenderHealthBar(
          barRect,
          centerProjectedCoord.y(),
          1.f,  // TODO: Determine fill amount based on HP
          (unit.GetPlayerIndex() < 0) ? gaiaColor : playerColors[unit.GetPlayerIndex()],
          healthBarShader,
          spritePointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight);
    }
  }
}
