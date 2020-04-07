// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QPointF>

#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/server/object.hpp"

/// Represents a unit on the server.
class ServerUnit : public ServerObject {
 public:
  ServerUnit(int playerIndex, UnitType type, const QPointF& mapCoord);
  
  inline UnitType GetUnitType() const { return type; }
  
  inline const QPointF& GetMapCoord() const { return mapCoord; }
  inline void SetMapCoord(const QPointF& mapCoord) { this->mapCoord = mapCoord; }
  
  inline UnitAction GetCurrentAction() const { return currentAction; }
  inline void SetCurrentAction(UnitAction newAction) { currentAction = newAction; }
  
  inline double GetCurrentActionStartTime() const { return currentActionStartTime; }
  inline void SetCurrentActionStartTime(double time) { currentActionStartTime = time; }
  
  /// Attempts to command the unit to interact with the given target object.
  /// If the unit cannot actually interact with that object, this call does nothing.
  void SetTarget(u32 targetObjectId, ServerObject* targetObject, bool isManualTargeting);
  void RemoveTarget();
  inline u32 GetTargetObjectId() const { return targetObjectId; }
  inline u32 GetManuallyTargetedObjectId() const { return manuallyTargetedObjectId; }
  
  /// Commands the unit to move to the given mapCoord.
  void SetMoveToTarget(const QPointF& mapCoord);
  inline bool HasMoveToTarget() const { return hasMoveToTarget; }
  inline const QPointF& GetMoveToTargetMapCoord() const { return moveToTarget; }
  
  // TODO: Accept more complex paths (rather than just a single target).
  inline bool HasPath() const { return hasPath; }
  inline void SetPath(const std::vector<QPointF>& reversePath) { hasPath = true; this->reversePath = reversePath; }
  inline void PauseMovement() { currentAction = UnitAction::Idle; }
  inline void StopMovement() { currentAction = UnitAction::Idle; hasMoveToTarget = false; hasPath = false; currentMovementDirection = QPointF(0, 0); }
  inline const QPointF& GetNextPathTarget() const { return reversePath.empty() ? moveToTarget : reversePath.back(); }
  inline void PathSegmentCompleted() { reversePath.pop_back(); if (reversePath.empty()) { hasPath = false; } }
  
  inline const QPointF& GetMovementDirection() const { return currentMovementDirection; }
  inline void SetMovementDirection(const QPointF& direction) { currentMovementDirection = direction; }
  
  inline ResourceType GetCarriedResourceType() const { return carriedResourceType; }
  inline void SetCarriedResourceType(ResourceType type) { carriedResourceType = type; }
  
  inline int GetCarriedResourceAmount() const { return static_cast<int>(carriedResourceAmount); }
  inline float GetCarriedResourceAmountInternalFloat() const { return carriedResourceAmount; }
  inline void SetCarriedResourceAmount(float amount) { carriedResourceAmount = amount; }
  
  // TODO: Load this from some database for each unit type
  inline float GetMoveSpeed() const { return (type == UnitType::Scout) ? 2.f : 1.f; }
  
 private:
  void SetTargetInternal(u32 targetObjectId, ServerObject* targetObject, bool isManualTargeting);
  
  
  UnitType type;
  QPointF mapCoord;
  
  UnitAction currentAction;
  
  /// The server time at which the current action started.
  /// Only used for actions where it matters (e.g., attacking).
  double currentActionStartTime;
  
  /// The unit's target object (if any). Set to kInvalidObjectId if the unit does not have a target.
  u32 targetObjectId = kInvalidObjectId;
  
  /// The last object that was targeted manually (by the player). For example, if the player sends
  /// a villager to gather gold, and the villager is currently walking back to a mining camp to drop
  /// off the gold it has gathered, then manuallyTargetedObjectId is the gold mine, and targetObjectId
  /// is the mining camp. This allows the villager to know that it should return to mine gold after dropping
  /// off its currently carried resources.
  u32 manuallyTargetedObjectId = kInvalidObjectId;
  
  // /// The target being attacked. The unit remembers this at the start of its attack animation.
  // /// This is to prevent the target from changing while the attack animation plays.
  // u32 attackTargetObjectId;
  
  /// The unit's map coord target (if any).
  bool hasMoveToTarget = false;
  QPointF moveToTarget;
  
  /// Whether reversePath is valid. TODO: Could be dropped now; could represent not having a path as reversePath being empty
  bool hasPath = false;
  /// The currenly planned path to the unit's target. The first entry is the last node in the path, thus "reverse".
  std::vector<QPointF> reversePath;
  
  /// The current movement direction of the unit for the current linear segment of its planned path.
  /// This is in general the only movement-related piece of information that the clients know about.
  /// If this changes, the clients that see the unit need to be notified.
  QPointF currentMovementDirection = QPointF(0, 0);
  
  /// Amount of resources carried (for villagers).
  float carriedResourceAmount = 0;
  
  // Type of resources carried (for villagers).
  ResourceType carriedResourceType = ResourceType::NumTypes;
};

/// Convenience function to cast a ServerUnit to a ServerObject.
/// Before using this, you must ensure that object->isUnit().
inline ServerUnit* AsUnit(ServerObject* object) {
  return static_cast<ServerUnit*>(object);
}
inline const ServerUnit* AsUnit(const ServerObject* object) {
  return static_cast<const ServerUnit*>(object);
}
