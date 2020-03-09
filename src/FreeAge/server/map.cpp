#include "FreeAge/server/map.hpp"

#include <cmath>
#include <vector>

#include <mango/core/endian.hpp>
#include <QPoint>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"
#include "FreeAge/server/building.hpp"
#include "FreeAge/server/unit.hpp"

ServerMap::ServerMap(int width, int height)
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

ServerMap::~ServerMap() {
  for (const auto& item : objects) {
    delete item.second;
  }
  delete[] elevation;
  delete[] occupied;
}

void ServerMap::GenerateRandomMap(int playerCount, int seed) {
  srand(seed);
  
  // Generate town centers. They are placed along a rectangle that is inset from the map edges.
  constexpr int kDistanceToMapBorder = 12;
  constexpr int kPositionVariance = 4;
  
  int rectangleWidth = width - 2 * kDistanceToMapBorder;
  int rectangleHeight = height - 2 * kDistanceToMapBorder;
  int rectangleEdgeLength = 2 * rectangleWidth + 2 * rectangleHeight;
  constexpr int kPositionOnRectangleVariance = 7;
  
  std::vector<QPoint> townCenterLocations(playerCount);
  std::vector<QPointF> townCenterCenters(playerCount);
  QSize townCenterSize = GetBuildingSize(BuildingType::TownCenter);
  for (int player = 0; player < playerCount; ++ player) {
    int positionOnRectangle =
        ((player * rectangleEdgeLength / playerCount) +
         (rand() % (2 * kPositionOnRectangleVariance + 1)) - kPositionOnRectangleVariance) % rectangleEdgeLength;
    
    if (positionOnRectangle < rectangleWidth) {
      townCenterLocations[player] = QPoint(kDistanceToMapBorder + positionOnRectangle, kDistanceToMapBorder);
    } else if (positionOnRectangle < rectangleWidth + rectangleHeight) {
      int offset = positionOnRectangle - rectangleWidth;
      townCenterLocations[player] = QPoint(kDistanceToMapBorder + rectangleWidth, kDistanceToMapBorder + offset);
    } else if (positionOnRectangle < 2 * rectangleWidth + rectangleHeight) {
      int offset = positionOnRectangle - (rectangleWidth + rectangleHeight);
      townCenterLocations[player] = QPoint(kDistanceToMapBorder + rectangleWidth - offset, kDistanceToMapBorder + rectangleHeight);
    } else {
      int offset = positionOnRectangle - (2 * rectangleWidth + rectangleHeight);
      townCenterLocations[player] = QPoint(kDistanceToMapBorder, kDistanceToMapBorder + rectangleHeight - offset);
    }
    
    townCenterLocations[player] +=
        QPoint((rand() % (2 * kPositionVariance + 1)) - kPositionVariance,
               (rand() % (2 * kPositionVariance + 1)) - kPositionVariance);
    
    townCenterCenters[player] = QPointF(
        townCenterLocations[player].x() + 0.5f * townCenterSize.width(),
        townCenterLocations[player].y() + 0.5f * townCenterSize.height());
    
    AddBuilding(player, BuildingType::TownCenter, townCenterLocations[player]);
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
            AddBuilding(-1, BuildingType::TreeOak, QPoint(x, y));
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
          AddUnit(player, (rand() % 2 == 0) ? UnitType::FemaleVillager : UnitType::MaleVillager, spawnLoc);
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
        AddUnit(player, UnitType::Scout, spawnLoc);
        break;
      }
    }
  }
}

void ServerMap::PlaceElevation(int tileX, int tileY, int elevationValue) {
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

void ServerMap::AddBuilding(int player, BuildingType type, const QPoint& baseTile) {
  // Insert into objects map
  objects.insert(std::make_pair(nextObjectID, new ServerBuilding(player, type, baseTile)));
  ++ nextObjectID;
  
  // Mark the occupied tiles as such
  QRect occupancyRect = GetBuildingOccupancy(type);
  for (int y = occupancyRect.y(); y < occupancyRect.y() + occupancyRect.height(); ++ y) {
    for (int x = occupancyRect.x(); x < occupancyRect.x() + occupancyRect.width(); ++ x) {
      occupiedAt(x, y) = true;
    }
  }
}

void ServerMap::AddUnit(int player, UnitType type, const QPointF& position) {
  objects.insert(std::make_pair(nextObjectID, new ServerUnit(player, type, position)));
  ++ nextObjectID;
}
