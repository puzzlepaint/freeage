#include "FreeAge/server/building.hpp"

ServerBuilding::ServerBuilding(int playerIndex, BuildingType type, const QPoint& baseTile)
    : ServerObject(ObjectType::Building, playerIndex),
      type(type),
      baseTile(baseTile) {}
