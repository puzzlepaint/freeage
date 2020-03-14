#pragma once

#include <QString>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"
#include "FreeAge/client/texture.hpp"

/// Base class for buildings and units on the client.
class ClientObject {
 public:
  inline ClientObject(ObjectType objectType, int playerIndex, double creationServerTime)
      : creationServerTime(creationServerTime),
        playerIndex(playerIndex),
        objectType(static_cast<int>(objectType)),
        isSelected(false) {}
  
  inline bool isBuilding() const { return objectType == 0; }
  
  inline bool isUnit() const { return objectType == 1; }
  
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  QString GetObjectName() const;
  const Texture* GetIconTexture() const;
  
  inline int GetPlayerIndex() const { return playerIndex; }
  
  inline bool IsSelected() const { return isSelected; }
  inline void SetIsSelected(bool selected) { isSelected = selected; }
  
  inline bool ShallBeDisplayed(double serverTime) { return serverTime >= creationServerTime; }
  
 protected:
  /// Server time of the creation of this object. Before this displayed time is reached,
  /// the object must not be displayed on the client.
  double creationServerTime;
  
  u8 playerIndex;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
  
  bool isSelected;
};
