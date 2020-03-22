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
  
  occupiedForUnits = new bool[width * height];
  occupiedForBuildings = new bool[width * height];
  
  // Initialize the elevation to zero everywhere, and the occupancy to free.
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      elevationAt(x, y) = 0;
      if (x < width && y < height) {
        occupiedForUnitsAt(x, y) = false;
        occupiedForBuildingsAt(x, y) = false;
      }
    }
  }
}

ServerMap::~ServerMap() {
  for (const auto& item : objects) {
    delete item.second;
  }
  delete[] elevation;
  delete[] occupiedForUnits;
  delete[] occupiedForBuildings;
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
    
    AddBuilding(player, BuildingType::TownCenter, townCenterLocations[player], /*buildPercentage*/ 100);
  }
  
  auto getRandomLocation = [&](float minDistanceToTCs, int* tileX, int* tileY) {
    bool ok;
    for (int attempt = 0; attempt < 1000; ++ attempt) {
      *tileX = rand() % width;
      *tileY = rand() % height;
      ok = true;
      for (int i = 0; i < playerCount; ++ i) {
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
  const int kNumForests = 11 * (width * height) / (50.f * 50.f) + 0.5f;  // TODO: Make configurable
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
          if (radius <= forestRadius && !occupiedForBuildingsAt(x, y)) {
            AddBuilding(-1, BuildingType::TreeOak, QPoint(x, y), /*buildPercentage*/ 100);
          }
        }
      }
    }
  }
  
  // Generate forage bushes, stone and gold mines
  for (int player = 0; player < playerCount; ++ player) {
    for (int i = 0; i < 3; ++ i) {
      BuildingType type;
      int count;
      int baseRadius;
      
      if (i == 0) {
        type = BuildingType::ForageBush;
        count = 5;
        baseRadius = 6;
      } else if (i == 1) {
        type = BuildingType::GoldMine;
        count = 5;
        baseRadius = 8;
      } else {  // if (i == 2) {
        type = BuildingType::StoneMine;
        count = 4;
        baseRadius = 9;
      }
      
      while (true) {
        // TODO: Prevent this from potentially being an endless loop
        float radius = baseRadius + 3 * ((rand() % 10000) / 10000.f);
        float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
        QPoint spawnLoc(
            townCenterCenters[player].x() + radius * sin(angle),
            townCenterCenters[player].y() + radius * cos(angle));
        if (spawnLoc.x() < 0 || spawnLoc.y() < 0 ||
            spawnLoc.x() >= width || spawnLoc.y() >= height ||
            occupiedForBuildingsAt(spawnLoc.x(), spawnLoc.y())) {
          continue;
        }
        
        for (int t = 0; t < count; ++ t) {
          AddBuilding(-1, type, spawnLoc, /*buildPercentage*/ 100);
          if (t == count - 1) {
            break;
          }
          
          // Proceed to a random neighboring tile that is not occupied.
          bool foundFreeSpace = false;
          int startDirection = rand() % 4;
          for (int dirOffset = 0; dirOffset < 4; ++ dirOffset) {
            int testDirection = (startDirection + dirOffset) % 4;
            QPoint testLoc(
                spawnLoc.x() + ((testDirection == 0) ? 1 : ((testDirection == 1) ? -1 : 0)),
                spawnLoc.y() + ((testDirection == 2) ? 1 : ((testDirection == 3) ? -1 : 0)));
            if (!(testLoc.x() < 0 || testLoc.y() < 0 ||
                  testLoc.x() >= width || testLoc.y() >= height) &&
                !occupiedForBuildingsAt(testLoc.x(), testLoc.y())) {
              spawnLoc = testLoc;
              foundFreeSpace = true;
              break;
            }
          }
          
          if (!foundFreeSpace) {
            // TODO: Prevent this from happening / retry in another place.
            break;
          }
        }
        break;
      }
    }
  }
  
  // Generate hills
  const int kHillMinDistanceFromTCs = maxElevation + 2 + 8;  // TODO: Make configurable
  const int numHills = 40 * (width * height) / (50.f * 50.f) + 0.5f;  // TODO: Make configurable
  for (int hill = 0; hill < numHills; ++ hill) {
    int tileX;
    int tileY;
    if (getRandomLocation(kHillMinDistanceFromTCs, &tileX, &tileY)) {
      int elevationValue = rand() % maxElevation;
      PlaceElevation(tileX, tileY, elevationValue);
    }
  }
  
  // Generate villagers
  for (int player = 0; player < playerCount; ++ player) {
    for (int villager = 0; villager < 3; ++ villager) {
      ServerUnit* newUnit = new ServerUnit(player, (rand() % 2 == 0) ? UnitType::FemaleVillager : UnitType::MaleVillager, QPointF(-1, -1));
      
      while (true) {
        // TODO: Prevent this from potentially being an endless loop
        float radius = 4 + 2 * ((rand() % 10000) / 10000.f);
        float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
        QPointF spawnLoc(
            townCenterCenters[player].x() + radius * sin(angle),
            townCenterCenters[player].y() + radius * cos(angle));
        if (!DoesUnitCollide(newUnit, spawnLoc)) {
          newUnit->SetMapCoord(spawnLoc);
          AddUnit(newUnit);
          break;
        }
      }
    }
  }
  
  // Generate scouts
  for (int player = 0; player < playerCount; ++ player) {
    ServerUnit* newUnit = new ServerUnit(player, UnitType::Scout, QPointF(-1, -1));
    
    while (true) {
      // TODO: Prevent this from potentially being an endless loop
      float radius = 6 + 2 * ((rand() % 10000) / 10000.f);
      float angle = 2 * M_PI * ((rand() % 10000) / 10000.f);
      QPointF spawnLoc(
          townCenterCenters[player].x() + radius * sin(angle),
          townCenterCenters[player].y() + radius * cos(angle));
      if (!DoesUnitCollide(newUnit, spawnLoc)) {
        newUnit->SetMapCoord(spawnLoc);
        AddUnit(newUnit);
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

bool ServerMap::DoesUnitCollide(ServerUnit* unit, const QPointF& mapCoord) {
  float radius = GetUnitRadius(unit->GetUnitType());
  
  // Test collision with the map bounds
  if (mapCoord.x() < radius ||
      mapCoord.y() < radius ||
      mapCoord.x() >= width - radius ||
      mapCoord.y() >= height - radius) {
    return true;
  }
  
  // Test collision with occupied space
  float squaredRadius = radius * radius;
  int minTileX = std::max<int>(0, mapCoord.x() - radius);
  int minTileY = std::max<int>(0, mapCoord.y() - radius);
  int maxTileX = std::min<int>(width - 1, mapCoord.x() + radius);
  int maxTileY = std::min<int>(height - 1, mapCoord.y() + radius);
  for (int tileY = minTileY; tileY <= maxTileY; ++ tileY) {
    for (int tileX = minTileX; tileX <= maxTileX; ++ tileX) {
      if (!occupiedForUnitsAt(tileX, tileY)) {
        continue;
      }
      
      // Compute the point within the tile that is closest to the unit
      QPointF closestPointInTile(
          std::max<float>(tileX, std::min<float>(tileX + 1, mapCoord.x())),
          std::max<float>(tileY, std::min<float>(tileY + 1, mapCoord.y())));
      
      QPointF offset = mapCoord - closestPointInTile;
      float squaredDistance = offset.x() * offset.x() + offset.y() * offset.y();
      if (squaredDistance < squaredRadius) {
        return true;
      }
    }
  }
  
  // Test collision with other units
  // TODO: Use some spatial access structure to reduce the number of tests.
  //       Probably store a list of units on each map tile.
  for (const auto& item : objects) {
    ServerObject* object = item.second;
    if (object->isUnit() && object != unit) {
      ServerUnit* otherUnit = static_cast<ServerUnit*>(object);
      
      float otherRadius = GetUnitRadius(otherUnit->GetUnitType());
      QPointF offset = otherUnit->GetMapCoord() - mapCoord;
      float squaredDistance = offset.x() * offset.x() + offset.y() * offset.y();
      if (squaredDistance < (radius + otherRadius) * (radius + otherRadius)) {
        return true;
      }
    }
  }
  
  return false;
}

ServerBuilding* ServerMap::AddBuilding(int player, BuildingType type, const QPoint& baseTile, float buildPercentage, u32* id, bool addOccupancy) {
  ServerBuilding* newBuilding = new ServerBuilding(player, type, baseTile, buildPercentage);
  int newId = AddBuilding(newBuilding, addOccupancy);
  if (id) {
    *id = newId;
  }
  return newBuilding;
}

u32 ServerMap::AddBuilding(ServerBuilding* newBuilding, bool addOccupancy) {
  // Insert into objects map
  objects.insert(std::make_pair(nextObjectID, newBuilding));
  ++ nextObjectID;
  
  // Mark the occupied tiles as such
  if (addOccupancy) {
    AddBuildingOccupancy(newBuilding);
  }
  
  return nextObjectID - 1;
}

void ServerMap::AddBuildingOccupancy(ServerBuilding* building) {
  SetBuildingOccupancy(building, true);
}

void ServerMap::RemoveBuildingOccupancy(ServerBuilding* building) {
  SetBuildingOccupancy(building, false);
}

ServerUnit* ServerMap::AddUnit(int player, UnitType type, const QPointF& position, u32* id) {
  ServerUnit* newUnit = new ServerUnit(player, type, position);
  u32 newId = AddUnit(newUnit);
  if (id) {
    *id = newId;
  }
  return newUnit;
}

u32 ServerMap::AddUnit(ServerUnit* newUnit) {
  objects.insert(std::make_pair(nextObjectID, newUnit));
  ++ nextObjectID;
  return nextObjectID - 1;
}

void ServerMap::SetBuildingOccupancy(ServerBuilding* building, bool occupied) {
  const QPoint& baseTile = building->GetBaseTile();
  QRect occupancyRect = GetBuildingOccupancy(building->GetBuildingType());
  for (int y = baseTile.y() + occupancyRect.y(), endY = baseTile.y() + occupancyRect.y() + occupancyRect.height(); y < endY; ++ y) {
    for (int x = baseTile.x() + occupancyRect.x(), endX = baseTile.x() + occupancyRect.x() + occupancyRect.width(); x < endX; ++ x) {
      occupiedForUnitsAt(x, y) = occupied;
    }
  }
  
  QSize buildingSize = GetBuildingSize(building->GetBuildingType());
  for (int y = baseTile.y(), endY = baseTile.y() + buildingSize.height(); y < endY; ++ y) {
    for (int x = baseTile.x(), endX = baseTile.x() + buildingSize.width(); x < endX; ++ x) {
      occupiedForBuildingsAt(x, y) = occupied;
    }
  }
}
