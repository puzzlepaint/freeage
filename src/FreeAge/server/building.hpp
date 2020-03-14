#pragma once

#include <vector>

#include <QPoint>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/server/object.hpp"
#include "FreeAge/common/unit_types.hpp"

class PlayerInGame;

/// Represents a building on the server.
class ServerBuilding : public ServerObject {
 public:
  ServerBuilding(int playerIndex, BuildingType type, const QPoint& baseTile);
  
  /// Returns whether this building can produce the given type of unit for the given player.
  /// Checks:
  /// * That the building can produce this unit in principle
  /// * That the player has the necessary civilization / technologies to produce this unit
  bool CanProduce(UnitType unitType, PlayerInGame* player);
  
  /// Adds a unit to the end of the production queue.
  inline void QueueUnit(UnitType type) { productionQueue.push_back(type); }
  
  /// Returns true if at least one unit is queued for production, false otherwise.
  /// If true is returned, the type of the currently produced unit is given in the type output parameter.
  bool IsUnitQueued(UnitType* type);
  
  /// Removes the currenly produced unit / the currently researched technology from the queue.
  void RemoveCurrentItemFromQueue();
  
  inline float GetProductionPercentage() const { return productionPercentage; }
  inline void SetProductionPercentage(float percentage) { productionPercentage = percentage; }
  
  inline BuildingType GetBuildingType() const { return type; }
  inline const QPoint& GetBaseTile() const { return baseTile; }
  
 private:
  // TODO: Allow to queue technologies as well
  std::vector<UnitType> productionQueue;
  
  /// The progress on the production of the first item in the productionQueue, in percent.
  float productionPercentage;
  
  BuildingType type;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  QPoint baseTile;
};
