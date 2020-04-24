// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/object.hpp"

#include "FreeAge/client/building.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/unit.hpp"

void ClientObject::UpdateFieldOfView(Map* map, int change) {
  if (isBuilding()) {
    const ClientBuilding* building = static_cast<const ClientBuilding*>(this);
    QPointF center = building->GetCenterMapCoord();
    map->UpdateFieldOfView(center.x(), center.y(), GetBuildingLineOfSight(building->GetType()), change);
  } else if (isUnit()) {
    const ClientUnit* unit = static_cast<const ClientUnit*>(this);
    int tileX = static_cast<int>(unit->GetMapCoord().x());
    int tileY = static_cast<int>(unit->GetMapCoord().y());
    map->UpdateFieldOfView(tileX + 0.5f, tileY + 0.5f, GetUnitLineOfSight(unit->GetType()), change);
  }
}

QString ClientObject::GetObjectName() const {
  if (isBuilding()) {
    const ClientBuilding* building = static_cast<const ClientBuilding*>(this);
    return building->GetBuildingName();
  } else if (isUnit()) {
    const ClientUnit* unit = static_cast<const ClientUnit*>(this);
    return unit->GetUnitName();
  }
  return "";
}

const Texture* ClientObject::GetIconTexture() const {
  if (isBuilding()) {
    const ClientBuilding* building = static_cast<const ClientBuilding*>(this);
    return building->GetIconTexture();
  } else if (isUnit()) {
    const ClientUnit* unit = static_cast<const ClientUnit*>(this);
    return unit->GetIconTexture();
  }
  return nullptr;
}

void ClientObject::GarrisonUnit(ClientUnit* unit) {
  garrisonedUnits.push_back(unit);
}

void ClientObject::UngarrisonUnit(ClientUnit* unit) {
  bool found = false;
  for (usize i = 0, size = garrisonedUnits.size(); i < size; ++ i) {
    if (garrisonedUnits[i] == unit) {
      garrisonedUnits.erase(garrisonedUnits.begin() + i);
      found = true;
      break;
    }
  }
  if (!found) {
    LOG(ERROR) << "Did not find unit to ungarrison in garrisonedUnits";
  }
}

InteractionType GetInteractionType(ClientObject* actor, ClientObject* target) {
  // TODO: There is a copy of this function in the server code. Can we merge these copies?
  
  if (actor->isUnit()) {
    ClientUnit* actorUnit = AsUnit(actor);
    
    if (target->isBuilding()) {
      ClientBuilding* targetBuilding = AsBuilding(target);
      
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

    // TODO: return InteractionType::Garrison only for targets that are mainly used for garrison, like the
    //       the transport ship and rams.
  }
  
  return InteractionType::Invalid;
}
