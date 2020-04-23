// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/server/object.hpp"

#include "FreeAge/server/building.hpp"
#include "FreeAge/server/unit.hpp"

void ServerObject::GarrisonUnit(u32 unitId) { 
  garrisonedUnits.emplace_back(unitId);
}

void ServerObject::UngarrisonUnit(u32 unitId) {
  garrisonedUnits.erase(std::remove(garrisonedUnits.begin(), garrisonedUnits.end(), unitId), garrisonedUnits.end());
}

InteractionType GetInteractionType(ServerObject* actor, ServerObject* target) {
  // TODO: There is a copy of this function in the client code. Can we merge these copies?
  
  if (actor->isUnit()) {
    ServerUnit* actorUnit = AsUnit(actor);
    
    if (target->isBuilding()) {
      ServerBuilding* targetBuilding = AsBuilding(target);
      
      if (IsVillager(actorUnit->GetType())) {
        if (targetBuilding->GetPlayerIndex() == actorUnit->GetPlayerIndex() &&
            !targetBuilding->IsCompleted()) {
          return InteractionType::Construct;
        } else if (targetBuilding->GetType() == BuildingType::ForageBush) {
          return InteractionType::CollectBerries;
        } else if (targetBuilding->GetType() == BuildingType::GoldMine) {
          return InteractionType::CollectGold;
        } else if (targetBuilding->GetType() == BuildingType::StoneMine) {
          return InteractionType::CollectStone;
        } else if (IsTree(targetBuilding->GetType())) {
          return InteractionType::CollectWood;
        } else if (actorUnit->GetCarriedResourceAmount() > 0 &&
                   IsDropOffPointForResource(targetBuilding->GetType(), actorUnit->GetCarriedResourceType())) {
          return InteractionType::DropOffResource;
        }
      }
    }
    
    if (target->GetPlayerIndex() != actor->GetPlayerIndex() &&
        target->GetPlayerIndex() != kGaiaPlayerIndex) {
      return InteractionType::Attack;
    }
    
    // TODO (maanoo): add Garrison, all cases cannot be handled here, the user has to use the garrison button in some cases
  }
  
  return InteractionType::Invalid;
}
