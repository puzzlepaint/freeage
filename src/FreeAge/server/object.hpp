// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <cmath>
#include <vector>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"

class ServerUnit;

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
  
  inline u32 GetHP() const { return std::round(hp); }
  inline float GetHPInternalFloat() const { return hp; };
  inline void SetHP(float newHP) { hp = newHP; }

  void GarrisonUnit(ServerUnit* unit);
  void UngarrisonUnit(ServerUnit* unit);

  // Returns the units that are garrisoned.
  inline const std::vector<ServerUnit*>& GetGarrisonedUnits() const { return garrisonedUnits; }
  inline int GetGarrisonedUnitsCount() const { return garrisonedUnits.size(); }
  
  // TODO: Add function to generate a string that represents the unit for logging and debugging.
  //       eg. Villager@(20,4)
  
 private:
  /// Current hitpoints of the object.
  /// For display on the client, those are rounded to the nearest integer.
  float hp;
  
  u8 playerIndex;
  
  // The units that are garrisoned.
  std::vector<ServerUnit*> garrisonedUnits;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
};

/// Returns how the actor can interact with the target.
InteractionType GetInteractionType(ServerObject* actor, ServerObject* target);
