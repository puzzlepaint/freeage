// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QString>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"
#include "FreeAge/client/texture.hpp"

class Map;

/// Base class for buildings and units on the client.
class ClientObject {
 public:
  inline ClientObject(ObjectType objectType, int playerIndex, u32 hp)
      : playerIndex(playerIndex),
        hp(hp),
        objectType(static_cast<int>(objectType)) {}
  
  void UpdateFieldOfView(Map* map, int change);
  
  inline bool isBuilding() const { return objectType == 0; }
  inline bool isUnit() const { return objectType == 1; }
  
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  QString GetObjectName() const;
  const Texture* GetIconTexture() const;
  
  inline int GetPlayerIndex() const { return playerIndex; }
  
  inline u32 GetHP() const { return hp; }
  inline void SetHP(u32 newHP) { hp = newHP; }
  
  void GarrisonUnit(u32 unitId);
  void UngarrisonUnit(u32 unitId);

  // Returns the IDs of the units that are garrisoned.
  inline const std::vector<u32>& GetGarrisonedUnits() const { return garrisonedUnits; }
  inline int GetGarrisonedUnitsCount() const { return garrisonedUnits.size(); }

 protected:
  /// Index of the player which this object belongs to.
  int playerIndex;
  
  /// Current hitpoints of the object.
  u32 hp;
  
  // The IDs of the units that are garrisoned.
  std::vector<u32> garrisonedUnits;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
};

/// Returns how the actor can interact with the target.
InteractionType GetInteractionType(ClientObject* actor, ClientObject* target);
