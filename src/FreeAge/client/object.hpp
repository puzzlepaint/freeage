// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QString>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"
#include "FreeAge/common/player.hpp"
#include "FreeAge/client/texture.hpp"

class Map;
class ClientUnit;

/// Base class for buildings and units on the client.
class ClientObject {
 public:
  inline ClientObject(ObjectType objectType, Player* player, u32 hp)
      : player(player),
        hp(hp),
        objectType(static_cast<int>(objectType)) {}

  virtual ~ClientObject() {}
  
  void UpdateFieldOfView(Map* map, int change);
  
  inline bool isBuilding() const { return objectType == 0; }
  inline bool isUnit() const { return objectType == 1; }
  
  inline ObjectType GetObjectType() const { return static_cast<ObjectType>(objectType); }
  QString GetObjectName() const;
  const Texture* GetIconTexture() const;
  
  inline const Player* GetPlayer() const { return player; }
  inline int GetPlayerIndex() const { return player->index; }
  
  inline u32 GetHP() const { return hp; }
  inline void SetHP(u32 newHP) { hp = newHP; }
  
  void GarrisonUnit(ClientUnit* unit);
  void UngarrisonUnit(ClientUnit* unit);

  // Returns the units that are garrisoned.
  inline const std::vector<ClientUnit*>& GetGarrisonedUnits() const { return garrisonedUnits; }
  inline int GetGarrisonedUnitsCount() const { return garrisonedUnits.size(); }
  
  // NOTE: For performance, GetStats() function should be preferred, when possible, over GetObjectStats().
  virtual const ObjectTypeStats& GetObjectStats() const = 0;

 protected:
  /// The player which this object belongs to.
  Player* player;
  
  /// Current hitpoints of the object.
  u32 hp;
  
  // The units that are garrisoned.
  std::vector<ClientUnit*> garrisonedUnits;
  
  /// 0 for buildings, 1 for units.
  u8 objectType;
};

/// Returns how the actor can interact with the target.
InteractionType GetInteractionType(ClientObject* actor, ClientObject* target);
