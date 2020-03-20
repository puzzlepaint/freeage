#pragma once

#include <unordered_map>

#include <QByteArray>
#include <QPoint>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/server/object.hpp"

class ServerBuilding;
class ServerUnit;

/// The server's representation of the game's map.
class ServerMap {
 public:
  /// Creates a new map with the given size in tiles.
  ServerMap(int width, int height);
  
  ~ServerMap();
  
  void GenerateRandomMap(int playerCount, int seed);
  
  /// Sets the given tile's elevation to the given value,
  /// while ensuring that the maximum slope of 1 is not exceeded
  /// (i.e., neighboring tiles may be modified as well).
  void PlaceElevation(int tileX, int tileY, int elevationValue);
  
  /// Adds a new building to the map and returns it. Optionally returns the new building's ID in id.
  /// Optionally calls AddBuildingOccupancy() on the building.
  ServerBuilding* AddBuilding(int player, BuildingType type, const QPoint& baseTile, float buildPercentage, u32* id = nullptr, bool addOccupancy = true);
  /// Adds the given building to the map and returns the ID that it received.
  /// Optionally calls AddBuildingOccupancy() on the building.
  u32 AddBuilding(ServerBuilding* newBuilding, bool addOccupancy = true);
  
  void AddBuildingOccupancy(ServerBuilding* building);
  void RemoveBuildingOccupancy(ServerBuilding* building);
  
  /// Adds a new unit to the map and returns it. Optionally returns the new unit's ID in id.
  ServerUnit* AddUnit(int player, UnitType type, const QPointF& position, u32* id = nullptr);
  /// Adds the given unit to the map and returns the ID that it received.
  u32 AddUnit(ServerUnit* newUnit);
  
  /// Tests whether the given unit could stand at the given mapCoord without
  /// colliding with other units or occupied space (buildings, etc.)
  bool DoesUnitCollide(ServerUnit* unit, const QPointF& mapCoord);
  
  /// Returns the elevation at the given tile corner.
  inline int& elevationAt(int cornerX, int cornerY) { return elevation[cornerY * (width + 1) + cornerX]; }
  inline const int& elevationAt(int cornerX, int cornerY) const { return elevation[cornerY * (width + 1) + cornerX]; }
  
  /// Returns the occupancy state at the given tile.
  inline bool& occupiedAt(int tileX, int tileY) { return occupied[tileY * width + tileX]; }
  inline const bool& occupiedAt(int tileX, int tileY) const { return occupied[tileY * width + tileX]; }
  
  inline std::unordered_map<u32, ServerObject*>& GetObjects() { return objects; }
  inline const std::unordered_map<u32, ServerObject*>& GetObjects() const { return objects; }
  
  inline int GetWidth() const { return width; }
  inline int GetHeight() const { return height; }
  
 private:
  /// The maximum possible elevation level (the lowest is zero).
  /// This may be higher than the maximum actually existing
  /// elevation level (but never lower).
  int maxElevation;
  
  /// 2D array storing the elevation level for each tile corner.
  /// The array size is thus: (width + 1) * (height + 1).
  /// An element (x, y) has index: [y * (width + 1) + x].
  int* elevation;
  
  /// 2D array storing whether each tile is occupied (for example,
  /// by a building). The array size is width * height.
  /// An element (x, y) has index: [y * width + x].
  bool* occupied;
  
  /// Width of the map in tiles.
  int width;
  
  /// Height of the map in tiles.
  int height;
  
  /// The next ID that will be given to the next added building or unit.
  u32 nextObjectID = 0;
  
  /// Map of object ID -> ServerObject*. The pointer is owned by the map.
  std::unordered_map<u32, ServerObject*> objects;
};
