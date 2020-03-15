#include "FreeAge/server/unit.hpp"

#include "FreeAge/server/building.hpp"

ServerUnit::ServerUnit(int playerIndex, UnitType type, const QPointF& mapCoord)
    : ServerObject(ObjectType::Unit, playerIndex),
      type(type),
      mapCoord(mapCoord) {}

void ServerUnit::SetTarget(u32 targetObjectId, ServerObject* targetObject) {
  if (IsVillager(type)) {
    // Is the target a building foundation that the villager could construct?
    if (targetObject->isBuilding()) {
      ServerBuilding* targetBuilding = static_cast<ServerBuilding*>(targetObject);
      if (targetBuilding->GetPlayerIndex() == GetPlayerIndex() &&
          targetBuilding->GetBuildPercentage() < 100) {
        // Make the villager construct the target building.
        SetTargetInternal(targetObjectId, targetObject);
        return;
      }
    }
  }
}

void ServerUnit::RemoveTarget() {
  targetObjectId = kInvalidObjectId;
}

void ServerUnit::SetMoveToTarget(const QPointF& mapCoord) {
  // The path will be computed on the next game state update.
  hasPath = false;
  
  moveToTarget = mapCoord;
  hasMoveToTarget = true;
  
  targetObjectId = kInvalidObjectId;
}

void ServerUnit::SetTargetInternal(u32 targetObjectId, ServerObject* targetObject) {
  // The path will be computed on the next game state update.
  hasPath = false;
  
  // TODO: For now, we simply make the unit move to the center of the target object.
  //       Actually, it should only move such as to touch the target object in any way (for melee interactions),
  //       or move to a location from where it can reach it (for ranged attacks).
  // TODO: In addition, if the target is a unit, the commanded unit needs to update its path
  //       if the target unit moves.
  if (targetObject->isBuilding()) {
    ServerBuilding* targetBuilding = static_cast<ServerBuilding*>(targetObject);
    QSize buildingSize = GetBuildingSize(targetBuilding->GetBuildingType());
    moveToTarget = targetBuilding->GetBaseTile() + 0.5f * QPointF(buildingSize.width(), buildingSize.height());
  } else if (targetObject->isUnit()) {
    ServerUnit* targetUnit = static_cast<ServerUnit*>(targetObject);
    moveToTarget = targetUnit->GetMapCoord();
  }
  hasMoveToTarget = true;
  
  this->targetObjectId = targetObjectId;
}
