#pragma once

#include <QPoint>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/server/object.hpp"

/// Represents a building on the server.
class ServerBuilding : public ServerObject {
 public:
  ServerBuilding(int playerIndex, BuildingType type, const QPoint& baseTile);
  
  inline BuildingType GetBuildingType() const { return type; }
  inline const QPoint& GetBaseTile() const { return baseTile; }
  
 private:
  BuildingType type;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  QPoint baseTile;
};
