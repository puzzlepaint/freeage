#include "FreeAge/server/object.hpp"

#include "FreeAge/server/building.hpp"
#include "FreeAge/server/unit.hpp"

InteractionType GetInteractionType(ServerObject* actor, ServerObject* target) {
  if (actor->isUnit()) {
    ServerUnit* actorUnit = static_cast<ServerUnit*>(actor);
    
    if (target->isBuilding()) {
      ServerBuilding* targetBuilding = static_cast<ServerBuilding*>(target);
      
      if (IsVillager(actorUnit->GetUnitType())) {
        if (targetBuilding->GetPlayerIndex() == actorUnit->GetPlayerIndex() &&
            targetBuilding->GetBuildPercentage() < 100) {
          return InteractionType::Construct;
        } else if (targetBuilding->GetBuildingType() == BuildingType::ForageBush) {
          return InteractionType::CollectBerries;
        } else if (targetBuilding->GetBuildingType() == BuildingType::GoldMine) {
          return InteractionType::CollectGold;
        } else if (targetBuilding->GetBuildingType() == BuildingType::StoneMine) {
          return InteractionType::CollectStone;
        } else if (IsTree(targetBuilding->GetBuildingType())) {
          return InteractionType::CollectWood;
        } else if (actorUnit->GetCarriedResourceAmount() > 0 &&
                   IsDropOffPointForResource(targetBuilding->GetBuildingType(), actorUnit->GetCarriedResourceType())) {
          return InteractionType::DropOffResource;
        }
      }
    }
  }
  
  return InteractionType::Invalid;
}
