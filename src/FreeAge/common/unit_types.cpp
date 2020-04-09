// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/unit_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"

float GetUnitRadius(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 0.15;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 0.15;
  case UnitType::Scout:
    return 0.3;
  default:
    LOG(ERROR) << "GetUnitRadius() called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}

QString GetUnitName(UnitType type) {
  // TODO: Load this from some data file
  switch (type) {
  case UnitType::FemaleVillager:
  case UnitType::MaleVillager:
    return QObject::tr("Villager");
  case UnitType::FemaleVillagerBuilder:
  case UnitType::MaleVillagerBuilder:
    return QObject::tr("Builder");
  case UnitType::FemaleVillagerForager:
  case UnitType::MaleVillagerForager:
    return QObject::tr("Forager");
  case UnitType::FemaleVillagerLumberjack:
  case UnitType::MaleVillagerLumberjack:
    return QObject::tr("Lumberjack");
  case UnitType::FemaleVillagerGoldMiner:
  case UnitType::MaleVillagerGoldMiner:
    return QObject::tr("Gold Miner");
  case UnitType::FemaleVillagerStoneMiner:
  case UnitType::MaleVillagerStoneMiner:
    return QObject::tr("Stone Miner");
  case UnitType::Militia:
    return QObject::tr("Militia");
  case UnitType::Scout:
    return QObject::tr("Scout Cavalry");
  case UnitType::NumUnits:
    LOG(ERROR) << "GetUnitName() called on UnitType::NumUnits";
    break;
  }
  
  return 0;
}

ResourceAmount GetUnitCost(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return ResourceAmount(0, 50, 0, 0);
  }
  
  switch (type) {
  case UnitType::Militia:
    return ResourceAmount(0, 60, 20, 0);
  case UnitType::Scout:
    return ResourceAmount(0, 80, 0, 0);
  default:
    LOG(ERROR) << "GetUnitName() called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return ResourceAmount(0, 0, 0, 0);
}

float GetUnitProductionTime(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 10;  // TODO: Set too short on purpose for testing
  }
  
  switch (type) {
  case UnitType::Militia:
    return 8;  // TODO: Set too short on purpose for testing
  case UnitType::Scout:
    return 4;  // TODO: Set too short on purpose for testing
  default:
    LOG(ERROR) << "GetUnitProductionTime() called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return -1;
}

int GetUnitAttackFrames(UnitType type) {
  // TODO: Load this from some data file
  // TODO: These could be extracted from the attack sprites.
  
  if (IsFemaleVillager(type)) {
    return 45;
  } else if (IsMaleVillager(type)) {
    return 60;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 30;
  case UnitType::Scout:
    return 30;
  default:
    LOG(ERROR) << "GetUnitAttackFrames() called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}

u32 GetUnitMaxHP(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 25;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 40;
  case UnitType::Scout:
    return 45;
  default:
    LOG(ERROR) << "Function called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}

u32 GetUnitMeleeAttack(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 3;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 4;
  case UnitType::Scout:
    return 3;
  default:
    LOG(ERROR) << "Function called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}

u32 GetUnitMeleeArmor(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 0;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 0;
  case UnitType::Scout:
    return 0;
  default:
    LOG(ERROR) << "Function called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}

float GetUnitLineOfSight(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 4;
  }
  
  switch (type) {
  case UnitType::Militia:
    return 4;
  case UnitType::Scout:
    return 4;
  default:
    LOG(ERROR) << "Function called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return 0;
}
