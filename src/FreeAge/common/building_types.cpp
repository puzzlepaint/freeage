// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/building_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"


QString GetBuildingName(BuildingType type) {
  switch (type) {
  case BuildingType::TownCenter: return QObject::tr("Town Center");
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingName() called on BuildingType::TownCenterBack"; return "";
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingName() called on BuildingType::TownCenterCenter"; return "";
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingName() called on BuildingType::TownCenterFront"; return "";
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingName() called on BuildingType::TownCenterMain"; return "";
  case BuildingType::House: return QObject::tr("House");
  
  case BuildingType::Mill: return QObject::tr("Mill");
  case BuildingType::MiningCamp: return QObject::tr("Mining Camp");
  case BuildingType::LumberCamp: return QObject::tr("Lumber Camp");
  case BuildingType::Dock: return QObject::tr("Dock");
  
  case BuildingType::Barracks: return QObject::tr("Barracks");
  case BuildingType::Outpost: return QObject::tr("Outpost");
  case BuildingType::PalisadeWall: return QObject::tr("Palisade Wall");
  case BuildingType::PalisadeGate: return QObject::tr("Palisade Gate");
  
  case BuildingType::TreeOak: return QObject::tr("Oak Tree");
  case BuildingType::ForageBush: return QObject::tr("Forage Bush");
  case BuildingType::GoldMine: return QObject::tr("Gold Mine");
  case BuildingType::StoneMine: return QObject::tr("Stone Mine");
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingName() called on BuildingType::NumBuildings"; return "";
  }
  return "";
}

int GetBuildingMaxInstances(BuildingType type) {
  if (type == BuildingType::TownCenter) { // TODO: add wonder
    return 1;
  } else if (type >= BuildingType::House && type <= BuildingType::PalisadeGate) {
    return -1; // unlimited
  }
  return 0;
}

int GetMaxElevationDifferenceForBuilding(BuildingType type) {
  if (type == BuildingType::TownCenter) {
    return 0;
  } else {
    return 2; // TODO: Check how the original game behaves; this is just made up currently
  }
}
