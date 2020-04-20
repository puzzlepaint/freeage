// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QString>

#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/resources.hpp"

/// Unit types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector.
enum class UnitType {
  FemaleVillager = 0,
  FemaleVillagerBuilder,
  FemaleVillagerForager,
  FemaleVillagerLumberjack,
  FemaleVillagerGoldMiner,
  FemaleVillagerStoneMiner,
  MaleVillager,
  MaleVillagerBuilder,
  MaleVillagerForager,
  MaleVillagerLumberjack,
  MaleVillagerGoldMiner,
  MaleVillagerStoneMiner,
  
  FirstVillager = FemaleVillager,
  LastVillager = MaleVillagerStoneMiner,
  
  Militia,
  
  Scout,
  
  NumUnits
};

/// Unit actions, leading to different animations.
/// TODO: Can we drop this and directly use the UnitAnimation enum instead?
enum class UnitAction {
  Idle = 0,
  Moving,
  Task,
  Attack,
  NumActions
};

inline bool IsVillager(UnitType type) {
  return type >= UnitType::FirstVillager &&
         type <= UnitType::LastVillager;
}
inline bool IsFemaleVillager(UnitType type) {
  return type >= UnitType::FemaleVillager &&
         type <= UnitType::FemaleVillagerStoneMiner;
}
inline bool IsMaleVillager(UnitType type) {
  return type >= UnitType::MaleVillager &&
         type <= UnitType::MaleVillagerStoneMiner;
}
inline ResourceType GetResourceTypeOfVillagerType(UnitType type) {
  if (type == UnitType::FemaleVillagerForager || type == UnitType::MaleVillagerForager) {
    return ResourceType::Food;
  } else if (type == UnitType::FemaleVillagerLumberjack || type == UnitType::MaleVillagerLumberjack) {
    return ResourceType::Wood;
  } else if (type == UnitType::FemaleVillagerGoldMiner || type == UnitType::MaleVillagerGoldMiner) {
    return ResourceType::Gold;
  } else if (type == UnitType::FemaleVillagerStoneMiner || type == UnitType::MaleVillagerStoneMiner) {
    return ResourceType::Stone;
  }
  return ResourceType::NumTypes;
}

float GetUnitRadius(UnitType type);
QString GetUnitName(UnitType type);

/// TODO: This needs to consider the player's civilization and researched technologies
ResourceAmount GetUnitCost(UnitType type);

/// Returns the production time for the unit in seconds.
/// TODO: This needs to consider the player's civilization and researched technologies
float GetUnitProductionTime(UnitType type);

int GetUnitAttackFrames(UnitType type);

/// TODO: This needs to consider the player's civilization and researched technologies
u32 GetUnitMaxHP(UnitType type);
Damage GetUnitDamage(UnitType type); // TODO: replace with GetAttack
Armor GetUnitArmor(UnitType type);

float GetUnitLineOfSight(UnitType type);
