#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"

/// Base class for game objects (buildings and units).
class ServerObject {
 public:
  inline ServerObject(ObjectType objectType, int playerIndex)
      : playerIndex(playerIndex),
        objectType(static_cast<int>(objectType)) {}
  
  inline bool isBuilding() const { return objectType == 0; }
  inline bool isUnit() const { return objectType == 1; }
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  
  inline int GetPlayerIndex() const { return playerIndex; }
  
 private:
  u8 playerIndex;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
};
