// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/unit_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"

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
