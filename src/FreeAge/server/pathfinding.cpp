// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/server/pathfinding.hpp"

#include <iostream>
#include <queue>

#include <QImage>
#include <QPoint>
#include <QRect>

#include "FreeAge/common/timing.hpp"
#include "FreeAge/common/util.hpp"
#include "FreeAge/server/building.hpp"
#include "FreeAge/server/map.hpp"
#include "FreeAge/server/unit.hpp"

/// Tests whether the unit could walk from p0 to p1 (or vice versa) without colliding
/// with a building. Notice that this function does not check whether the start and
/// end points themselves are (fully) free, it only checks the space between them.
static bool IsPathFree(float unitRadius, const QPointF& p0, const QPointF& p1, const QRect& openRect, ServerMap* map) {
  // Obtain the points to the right and left of p0 and p1.
  constexpr float kErrorEpsilon = 1e-3f;
  
  QPointF p0ToP1 = p1 - p0;
  QPointF right(
      -p0ToP1.y(),
      p0ToP1.x());
  right *= (unitRadius + kErrorEpsilon) / std::max(1e-4f, Length(right));
  
  QPointF p0Right = p0 + right;
  QPointF p0Left = p0 - right;
  QPointF p1Right = p1 + right;
  QPointF p1Left = p1 - right;
  
  // Rasterize the polygon defined by all the points into the map grid.
  int minRow = std::numeric_limits<int>::max();
  int maxRow = 0;
  std::vector<std::pair<int, int>> rowRanges(map->GetHeight(), std::make_pair(std::numeric_limits<int>::max(), 0));
  
  auto rasterize = [&](int x, int y) {
    // For safety, clamp the coordinate to the map area.
    x = std::max(0, std::min(map->GetWidth() - 1, x));
    y = std::max(0, std::min(map->GetHeight() - 1, y));
    
    minRow = std::min(minRow, y);
    maxRow = std::max(maxRow, y);
    
    auto& rowRange = rowRanges[y];
    rowRange.first = std::min(rowRange.first, x);
    rowRange.second = std::max(rowRange.second, x);
  };
  
  auto rasterizeLine = [&](const QPointF& start, const QPointF& end) {
    QPointF cur = start;
    QPointF remaining = end - start;
    
    int x = static_cast<int>(cur.x());
    int y = static_cast<int>(cur.y());
    
    int endX = static_cast<int>(end.x());
    int endY = static_cast<int>(end.y());
    
    int sx = (remaining.x() > 0) ? 1 : -1;
    int sy = (remaining.y() > 0) ? 1 : -1;
    
    while (true) {
      rasterize(x, y);
      
      float fx = cur.x() - x;
      float fy = cur.y() - y;
      
      float xToBorder = (sx > 0) ? (1 - fx) : fx;
      float yToBorder = (sy > 0) ? (1 - fy) : fy;
      
      if (fabs(remaining.x()) <= xToBorder &&
          fabs(remaining.y()) <= yToBorder) {
        // The goal is in the current square.
        break;
      }
      if ((endX - x) * sx <= 0 &&
          (endY - y) * sy <= 0) {
        // We somehow surpassed the end without noticing.
        LOG(WARNING) << "Emergency exit.";
        break;
      }
      
      float diffX, diffY;
      if (fabs(remaining.x()) / std::max<float>(1e-5f, fabs(remaining.y())) >
          xToBorder / std::max(1e-5f, yToBorder)) {
        // Go in x direction
        if (sx > 0) {
          // Go to the right.
          diffX = - (1 - fx);
        } else {
          // Go to the left.
          diffX = fx;
        }
        diffY = remaining.y() * ((remaining.x() + diffX) / remaining.x() - 1);
        x += sx;
      } else {
        // Go in y direction
        if (sy > 0) {
          // Go to the bottom.
          diffY = - (1 - fy);
        } else {
          // Go to the top.
          diffY = fy;
        }
        diffX = remaining.x() * ((remaining.y() + diffY) / remaining.y() - 1);
        y += sy;
      }
      
      remaining.setX(remaining.x() + diffX);
      remaining.setY(remaining.y() + diffY);
      cur.setX(cur.x() - diffX);
      cur.setY(cur.y() - diffY);
    }
  };
  
  rasterizeLine(p0Right, p1Right);
  rasterizeLine(p0Left, p1Left);
  rasterizeLine(p0Right, p0Left);
  rasterizeLine(p1Right, p1Left);
  
  // Test whether any tile within the boundaries of the rasterized area is occupied.
  // If yes, the path is blocked.
  constexpr bool kDebugRasterization = false;
  constexpr const char* kDebugImagePath = "/tmp/FreeAge_pathFree_debug.png";
  if (kDebugRasterization) {
    QImage debugImage(map->GetWidth(), map->GetHeight(), QImage::Format_RGB32);
    debugImage.fill(qRgb(255, 255, 255));
    
    for (int row = minRow; row <= maxRow; ++ row) {
      auto& rowRange = rowRanges[row];
      for (int col = rowRange.first; col <= rowRange.second; ++ col) {
        if (map->occupiedForUnitsAt(col, row) && !openRect.contains(col, row, false)) {
          debugImage.setPixelColor(col, row, qRgb(255, 0, 0));
        } else {
          debugImage.setPixelColor(col, row, qRgb(0, 255, 0));
        }
      }
    }
    
    LOG(WARNING) << "Saving IsPathFree() debug image to " << kDebugImagePath;
    debugImage.save(kDebugImagePath);
    
    char dummy;
    std::cin >> dummy;
  }
  
  for (int row = minRow; row <= maxRow; ++ row) {
    auto& rowRange = rowRanges[row];
    for (int col = rowRange.first; col <= rowRange.second; ++ col) {
      if (map->occupiedForUnitsAt(col, row) && !openRect.contains(col, row, false)) {
        return false;
      }
    }
  }
  
  return true;
}

void PlanUnitPath(ServerUnit* unit, ServerMap* map) {
  constexpr bool kOutputPathfindingDebugMessages = false;
  
  Timer pathPlanningTimer;
  
  typedef float CostT;
  
  int mapWidth = map->GetWidth();
  int mapHeight = map->GetHeight();
  
  // Determine the tile that the unit stands on. This will be the start tile.
  QPoint start(
      std::max(0, std::min(mapWidth - 1, static_cast<int>(unit->GetMapCoord().x()))),
      std::max(0, std::min(mapHeight - 1, static_cast<int>(unit->GetMapCoord().y()))));
  
  // Determine the goal tiles and treat them as open even if they are occupied.
  // This is done for the tiles taken up by the unit's target.
  // This allows us to plan a path "into" the target.
  QRect goalRect;
  if (unit->GetTargetObjectId() != kInvalidObjectId) {
    auto targetIt = map->GetObjects().find(unit->GetTargetObjectId());
    if (targetIt != map->GetObjects().end()) {
      ServerObject* targetObject = targetIt->second;
      if (targetObject->isBuilding()) {
        ServerBuilding* targetBuilding = AsBuilding(targetObject);
        
        const QPoint& baseTile = targetBuilding->GetBaseTile();
        QSize buildingSize = GetBuildingSize(targetBuilding->GetType());
        goalRect = QRect(baseTile, buildingSize);
      }
    }
  }
  if (goalRect.isNull()) {
    goalRect = QRect(
        std::max(0, std::min(mapWidth - 1, static_cast<int>(unit->GetMoveToTargetMapCoord().x()))),
        std::max(0, std::min(mapHeight - 1, static_cast<int>(unit->GetMoveToTargetMapCoord().y()))),
        1,
        1);
  }
  
  // Use A* to plan a path from the start to the goal tile.
  // * Treat unit-occupied tiles as obstacles.
  // * Treat tiles that are occupied by the unit's target building (if any) as free,
  //   such that the algorithm can plan a path "into" the goal.
  // * If the goal is not reachable, return the path that leads to the reachable
  //   position that is closest to the goal.
  struct Location {
    QPoint loc;
    float priority;
    
    inline Location(const QPoint& loc, float priority)
        : loc(loc),
          priority(priority) {}
    
    inline bool operator> (const Location& other) const {
      return priority > other.priority;
    }
  };
  
  std::priority_queue<Location, std::vector<Location>, std::greater<Location>> priorityQueue;
  
  priorityQueue.emplace(start, 0.f);
  
  std::vector<CostT> costSoFar(mapWidth * mapHeight);
  // TODO: Perhaps some potential overhead of this may be reduced by doing a memset() to zero instead,
  //       using "negative zero" for the start node, and later comparing to "positive zero" as a special case using std::signbit()?
  std::fill(std::begin(costSoFar), std::end(costSoFar), std::numeric_limits<float>::infinity());
  costSoFar[start.x() + mapWidth * start.y()] = 0;
  
  // Directions are encoded as row-major indices of grid cells in a 4x4 grid,
  // with (1, 1) being the origin of movement. A 3x3 grid would suffice,
  // however, with a 4x4 grid, computations are faster. So, for example,
  // the value 0 corresponds to cell (0, 0) in the grid, which has an offset of (-1, -1)
  // from the movement origin. So, the movement came from (-1, -1).
  // Second example: The value 4 corresponds to cell (0, 1) with movement (-1, 0).
  // The value 5 corresponds to zero movement, this is used for the start and for initialization.
  constexpr u8 cameFromUninitializedValue = 5;
  std::vector<u8> cameFrom(mapWidth * mapHeight);
  memset(cameFrom.data(), cameFromUninitializedValue, mapWidth * mapHeight);
  
  CostT smallestReachedHeuristicValue = std::numeric_limits<CostT>::max();
  QPoint smallestReachedHeuristicTile(-1, -1);
  
  static int numNeighborsToCheckArray[11] = {
    3,
    7,
    3,
    0,  // no valid direction
    7,
    8,
    7,
    0,  // no valid direction
    3,
    7,
    3,
  };
  static QPoint neighborsToCheckArray[11][8] = {
    {QPoint(1, 0), QPoint(0, 1), /*dependent on previous being free*/ QPoint(1, 1)},
    {QPoint(0, 1), /*occ. test*/ QPoint(1, -1), QPoint(1, 0), QPoint(1, 1), /*occ. test*/ QPoint(-1, -1), QPoint(-1, 0), QPoint(-1, 1)},
    {QPoint(-1, 0), QPoint(0, 1), /*dependent on previous being free*/ QPoint(-1, 1)},
    {},
    {QPoint(1, 0), /*occ. test*/ QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), /*occ. test*/ QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)},
    {QPoint(0, -1), QPoint(-1, 0), QPoint(1, 0), QPoint(0, 1), QPoint(1, 1), QPoint(-1, -1), QPoint(1, -1), QPoint(-1, 1)},
    {QPoint(-1, 0), /*occ. test*/ QPoint(1, -1), QPoint(0, -1), QPoint(-1, -1), /*occ. test*/ QPoint(1, 1), QPoint(0, 1), QPoint(-1, 1)},
    {},
    {QPoint(0, -1), QPoint(1, 0), /*dependent on previous being free*/ QPoint(1, -1)},
    {QPoint(0, -1), /*occ. test*/ QPoint(1, 1), QPoint(1, 0), QPoint(1, -1), /*occ. test*/ QPoint(-1, 1), QPoint(-1, 0), QPoint(-1, -1)},
    {QPoint(-1, 0), QPoint(0, -1), /*dependent on previous being free*/ QPoint(-1, -1)},
  };
  // Here we encode that diagonal movements require the two adjacent tiles to be free.
  static u8 neighborsRequireFreeTiles[11][8] = {
    {0, 0, 0b11},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0b11},
    {},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0, 0, 0b1100, 0b0011, 0b0101, 0b1010},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {},
    {0, 0, 0b11},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0b11},
  };
  
  int debugConsideredNodesCount = 0;
  
  // Set this to true to have a debug image written to /tmp/FreeAge_pathfinding_debug.png.
  // Legend:
  // * Black: Occupied tiles.
  // * Dark green: open rect.
  // * White: Free tiles, never considered by pathfinding.
  // * Light red: Free tiles, considered by pathfinding.
  // * Light yellow: Free tiles, added to the priority queue as a neighbor but not directly considered.
  // * Green: Free tiles that are part of the final path.
  constexpr bool kOutputDebugImage = false;
  constexpr const char* kDebugImagePath = "/tmp/FreeAge_pathfinding_debug.png";
  QImage debugImage;
  if (kOutputDebugImage) {
    debugImage = QImage(mapWidth, mapHeight, QImage::Format_RGB32);
    for (int y = 0; y < mapHeight; ++ y) {
      for (int x = 0; x < mapWidth; ++ x) {
        if (map->occupiedForUnitsAt(x, y)) {
          debugImage.setPixel(x, y, qRgb(0, 0, 0));
        } else {
          debugImage.setPixel(x, y, qRgb(255, 255, 255));
        }
      }
    }
    for (int y = goalRect.y(); y < goalRect.bottom(); ++ y) {
      for (int x = goalRect.x(); x < goalRect.right(); ++ x) {
        debugImage.setPixel(x, y, qRgb(0, 100, 0));
      }
    }
  }
  
  QPoint reachedGoalTile(-1, -1);
  while (!priorityQueue.empty()) {
    Location current = priorityQueue.top();
    priorityQueue.pop();
    
    ++ debugConsideredNodesCount;
    if (kOutputDebugImage) {
      debugImage.setPixel(current.loc.x(), current.loc.y(), qRgb(255, 127, 127));
    }
    
    if (goalRect.contains(current.loc, false)) {
      reachedGoalTile = current.loc;
      break;
    }
    
    int currentGridIndex = current.loc.x() + mapWidth * current.loc.y();
    CostT currentCost = costSoFar[currentGridIndex];
    int currentCameFrom = cameFrom[currentGridIndex];
    
    int numNeighborsToCheck = numNeighborsToCheckArray[currentCameFrom];
    QPoint* neighbors = neighborsToCheckArray[currentCameFrom];
    u8 freeNeighbors = 0;  // bitmask with a 1 for each free neighbor
    
    for (int neighborIdx = 0; neighborIdx < numNeighborsToCheck; ++ neighborIdx) {
      QPoint neighborDir = neighbors[neighborIdx];
      QPoint nextTile = current.loc + neighborDir;
      int nextGridIndex = nextTile.x() + mapWidth * nextTile.y();
      
      // Special case:
      // For straight movements, numNeighborsToCheck is 7. It contains two sets of directions preceded by an occupancy check.
      // This means that we only consider those directions if the occupancy-checked tile is occupied.
      // The occupancy check is at neighbor indices 1 and 4 and each applies to the two following neighbors.
      int skipLenght = 0;
      if (numNeighborsToCheck == 7 &&
          (neighborIdx == 1 || neighborIdx == 4)) {
        // This neighbor is an occupancy check. If it is occupied, we need to consider the following
        // two neighbors, otherwise we can skip them.
        skipLenght = 2;
      } else {
        u8 requiredFreeNeighbors = neighborsRequireFreeTiles[currentCameFrom][neighborIdx];
        if ((freeNeighbors & requiredFreeNeighbors) != requiredFreeNeighbors) {
          // The required previous free neighbors are not free.
          continue;
        }
      }
      
      // Skip neighbor if it is outside of the map.
      if (nextTile.x() < 0 || nextTile.y() < 0 ||
          nextTile.x() >= mapWidth || nextTile.y() >= mapHeight) {
        neighborIdx += skipLenght;
        continue;
      }
      // Skip neighbor if it is occupied.
      if (map->occupiedForUnitsAt(nextTile.x(), nextTile.y()) && !goalRect.contains(nextTile, false)) {
        // Continue while not skipping over possible neighbors depending on this as an occupancy check (since the check returned true).
        continue;
      }
      
      freeNeighbors |= 1 << neighborIdx;
      
      // Skip this if it is a failed occupancy check (instead of an actual neighbor).
      if (skipLenght > 0) {
        // If this was an occupancy check, but the tile is free, continue while skipping over the dependent neighbors.
        neighborIdx += skipLenght;
        continue;
      }
      
      // Compute the cost to reach this neighbor from the start.
      constexpr float sqrt2 = 1.41421356237310f;
      CostT newCost = currentCost + ((neighborDir.manhattanLength() == 2) ? sqrt2 : 1);
      
      // If the cost is better than the best cost known so far, expand the path to this neighbor.
      CostT* nextCostSoFar = &costSoFar[nextGridIndex];
      if (newCost < *nextCostSoFar) {
        *nextCostSoFar = newCost;
        
        // Compute the "diagonal distance" as a heuristic for the remaining path length to the goal.
        // This is a distance metric on the grid while allowing diagonal movements.
        int goalX = std::max(goalRect.x(), std::min(goalRect.x() + goalRect.width() - 1, nextTile.x()));
        int goalY = std::max(goalRect.y(), std::min(goalRect.y() + goalRect.height() - 1, nextTile.y()));
        int xDiff = std::abs(nextTile.x() - goalX);
        int yDiff = std::abs(nextTile.y() - goalY);
        int minDiff = std::min(xDiff, yDiff);
        int maxDiff = std::max(xDiff, yDiff);
        CostT heuristic = minDiff * sqrt2 + (maxDiff - minDiff) * 1;
        
        // Remember the closest tile to the goal that we found. This becomes important
        // in case we cannot reach the goal at all.
        if (heuristic < smallestReachedHeuristicValue) {
          smallestReachedHeuristicValue = heuristic;
          smallestReachedHeuristicTile = nextTile;
        }
        
        priorityQueue.emplace(nextTile, newCost + heuristic);
        cameFrom[nextGridIndex] = ((-neighborDir.x()) + 1) + 4 * ((-neighborDir.y()) + 1);
        
        if (kOutputDebugImage) {
          debugImage.setPixel(nextTile.x(), nextTile.y(), qRgb(255, 255, 127));
        }
      }
    }
  }
  
  if (kOutputPathfindingDebugMessages) {
    LOG(1) << "Pathfinding: considered " << debugConsideredNodesCount << " nodes (max possible: " << (mapWidth * mapHeight) << ")";
  }
  
  // Did we find a path to the goal or only to some other tile that is close to the goal?
  QPoint targetTile;
  if (reachedGoalTile.x() < 0) {
    // No path to the goal was found. Go to the reachable node that is closest to the goal.
    if (smallestReachedHeuristicTile.x() >= 0) {
      if (kOutputPathfindingDebugMessages) {
        LOG(1) << "Pathfinding: Goal not reached; going as close as possible";
      }
      targetTile = smallestReachedHeuristicTile;
    } else {
      if (kOutputPathfindingDebugMessages) {
        LOG(1) << "Pathfinding: Goal not reached and there is no better tile than the initial one. Stopping.";
      }
      unit->StopMovement();
    }
  } else {
    if (kOutputPathfindingDebugMessages) {
      LOG(1) << "Pathfinding: Goal reached";
    }
    targetTile = reachedGoalTile;
  }
  
  // Reconstruct the path, tracking back from "targetTile" using "cameFrom".
  // We leave out the start tile since the unit is already within that tile.
  std::vector<QPointF> reversePath;
  QPoint currentTile = targetTile;
  while (currentTile != start) {
    reversePath.push_back(QPointF(currentTile.x() + 0.5f, currentTile.y() + 0.5f));
    
    if (kOutputDebugImage) {
      debugImage.setPixel(currentTile.x(), currentTile.y(), qRgb(0, 255, 0));
    }
    
    int cameFromDirection = cameFrom[currentTile.x() + mapWidth * currentTile.y()];
    if (cameFromDirection >= 11 || numNeighborsToCheckArray[cameFromDirection] == 0 || cameFromDirection == 5) {
      LOG(ERROR) << "Erroneous value in cameFrom[] while reconstructing path: " << cameFromDirection;
      break;
    }
    
    int cameFromX = (cameFromDirection % 4) - 1;
    currentTile.setX(currentTile.x() + cameFromX);
    
    int cameFromY = (cameFromDirection / 4) - 1;
    currentTile.setY(currentTile.y() + cameFromY);
  }
  if (kOutputDebugImage) {
    debugImage.setPixel(start.x(), start.y(), qRgb(0, 255, 0));
    
    LOG(WARNING) << "Writing pathfinding debug image to: " << kDebugImagePath;
    debugImage.save(kDebugImagePath);
  }
  
  // Replace the last point with the exact goal location (if we can reach the goal)
  // TODO: If we can't reach the goal, maybe append a point here that makes the unit walk into the obstacle?
  if (reachedGoalTile.x() >= 0) {
    if (reversePath.empty()) {
      reversePath.push_back(unit->GetMoveToTargetMapCoord());
    } else if (goalRect.width() == 1 && goalRect.height() == 1) {
      reversePath[0] = unit->GetMoveToTargetMapCoord();
    }
  }
  
  if (kOutputPathfindingDebugMessages) {
    LOG(1) << "Pathfinding: Non-smoothed path length is " << reversePath.size();
  }
  
  // Smooth the planned path by attempting to drop corners.
  float unitRadius = GetUnitRadius(unit->GetType());
  for (usize i = 1; i < reversePath.size(); ++ i) {
    const QPointF& p0 = (i == reversePath.size() - 1) ? unit->GetMapCoord() : reversePath[i + 1];
    const QPointF& p1 = reversePath[i - 1];
    
    if (IsPathFree(unitRadius, p0, p1, goalRect, map)) {
      reversePath.erase(reversePath.begin() + i);
      -- i;
    }
  }
  
  if (kOutputPathfindingDebugMessages) {
    LOG(1) << "Pathfinding: Smoothed path length is " << reversePath.size();
    LOG(1) << "Pathfinding: Took " << pathPlanningTimer.Stop(false) << " s" << (kOutputDebugImage ? " (not accurate since kOutputDebugImage is true!)" : "");
  }
  
  // Assign the path to the unit.
  unit->SetPath(reversePath);
  
  // Start traversing the path:
  // Set the unit's movement direction to the first segment of the path.
  QPointF direction = unit->GetNextPathTarget() - unit->GetMapCoord();
  direction = direction / std::max(1e-4f, Length(direction));
  unit->SetMovementDirection(direction);
}
