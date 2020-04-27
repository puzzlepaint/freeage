// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/server/unit.hpp"

#include "FreeAge/server/building.hpp"

ServerUnit::ServerUnit(Player* player, UnitType type, const QPointF& mapCoord)
    : ServerObject(ObjectType::Unit, player),
      type(type),
      mapCoord(mapCoord),
      currentAction(UnitAction::Idle) {
  SetHP(GetStats().maxHp);
}

void ServerUnit::SetTarget(u32 targetObjectId, ServerObject* targetObject, bool isManualTargeting, InteractionType interaction) {
  targetObjectInteraction = interaction;
  if (interaction == InteractionType::Default) {
    interaction = GetInteractionType(this, targetObject);
    // Keeps the targetObjectInteraction with the value of Unknown.
  }

  if (interaction == InteractionType::Construct) {
    type = IsMaleVillager(type) ? UnitType::MaleVillagerBuilder : UnitType::FemaleVillagerBuilder;
  } else if (interaction == InteractionType::CollectBerries) {
    type = IsMaleVillager(type) ? UnitType::MaleVillagerForager : UnitType::FemaleVillagerForager;
  } else if (interaction == InteractionType::CollectWood) {
    type = IsMaleVillager(type) ? UnitType::MaleVillagerLumberjack : UnitType::FemaleVillagerLumberjack;
  } else if (interaction == InteractionType::CollectGold) {
    type = IsMaleVillager(type) ? UnitType::MaleVillagerGoldMiner : UnitType::FemaleVillagerGoldMiner;
  } else if (interaction == InteractionType::CollectStone) {
    type = IsMaleVillager(type) ? UnitType::MaleVillagerStoneMiner : UnitType::FemaleVillagerStoneMiner;
  } else if (interaction == InteractionType::DropOffResource ||
      interaction == InteractionType::Attack ||
      interaction == InteractionType::Garrison) {
    // no change
  } else {
    LOG(WARNING) << "ServerUnit::SetTarget() did not handle the interaction type.";
    return;
  }

  SetTargetInternal(targetObjectId, targetObject, isManualTargeting);
}

void ServerUnit::RemoveTarget() {
  targetObjectId = kInvalidObjectId;
  targetObjectInteraction = InteractionType::Default;
}

void ServerUnit::SetMoveToTarget(const QPointF& mapCoord) {
  // The path will be computed on the next game state update.
  hasPath = false;
  
  moveToTarget = mapCoord;
  hasMoveToTarget = true;
  
  targetObjectId = kInvalidObjectId;
  manuallyTargetedObjectId = kInvalidObjectId;
}

void ServerUnit::SetTargetInternal(u32 targetObjectId, ServerObject* targetObject, bool isManualTargeting) {
  // The path will be computed on the next game state update.
  hasPath = false;
  
  if (targetObject->isBuilding()) {
    ServerBuilding* targetBuilding = AsBuilding(targetObject);
    QSize buildingSize = targetBuilding->GetStats().size;
    moveToTarget = targetBuilding->GetBaseTile() + 0.5f * QPointF(buildingSize.width(), buildingSize.height());
  } else if (targetObject->isUnit()) {
    ServerUnit* targetUnit = AsUnit(targetObject);
    moveToTarget = targetUnit->GetMapCoord();
  }
  hasMoveToTarget = true;
  
  if (currentAction != UnitAction::Attack) {
    this->targetObjectId = targetObjectId;
  }
  if (isManualTargeting) {
    manuallyTargetedObjectId = targetObjectId;
  }
}
