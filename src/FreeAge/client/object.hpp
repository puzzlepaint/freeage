#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"

/// Base class for buildings and units on the client.
class ClientObject {
 public:
  inline ClientObject(ObjectType objectType, int playerIndex)
      : playerIndex(playerIndex),
        objectType(static_cast<int>(objectType)),
        isSelected(false) {}
  
  inline bool isBuilding() const { return objectType == 0; }
  inline bool isUnit() const { return objectType == 1; }
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  
  inline int GetPlayerIndex() const { return playerIndex; }
  
  inline bool IsSelected() const { return isSelected; }
  inline void SetIsSelected(bool selected) { isSelected = selected; }
  
 protected:
  u8 playerIndex;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
  
  bool isSelected;
};
