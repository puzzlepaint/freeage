#pragma once

#include <QString>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"
#include "FreeAge/client/texture.hpp"

/// Base class for buildings and units on the client.
class ClientObject {
 public:
  inline ClientObject(ObjectType objectType, int playerIndex, u32 hp)
      : playerIndex(playerIndex),
        hp(hp),
        objectType(static_cast<int>(objectType)),
        isSelected(false) {}
  
  inline bool isBuilding() const { return objectType == 0; }
  inline bool isUnit() const { return objectType == 1; }
  
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  QString GetObjectName() const;
  const Texture* GetIconTexture() const;
  
  inline int GetPlayerIndex() const { return playerIndex; }
  
  inline u32 GetHP() const { return hp; }
  inline void SetHP(u32 newHP) { hp = newHP; }
  
  inline bool IsSelected() const { return isSelected; }
  inline void SetIsSelected(bool selected) { isSelected = selected; }
  
 protected:
  /// Index of the player which this object belongs to.
  int playerIndex;
  
  /// Current hitpoints of the object.
  u32 hp;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
  
  /// Helper variable saying whether this object is currently selected.
  bool isSelected;
};
