#include "FreeAge/common/building_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"

QSize GetBuildingSize(BuildingType type) {
  // TODO: Load this from some data file?
  
  if (static_cast<int>(type) >= static_cast<int>(BuildingType::FirstTree) &&
      static_cast<int>(type) <= static_cast<int>(BuildingType::LastTree)) {
    return QSize(1, 1);
  }
  
  switch (type) {
  case BuildingType::TownCenter:
    return QSize(4, 4);
  case BuildingType::House:
    return QSize(2, 2);
  case BuildingType::Mill:
    return QSize(2, 2);
  case BuildingType::MiningCamp:
    return QSize(2, 2);
  case BuildingType::LumberCamp:
    return QSize(2, 2);
  case BuildingType::Dock:
    return QSize(3, 3);
  case BuildingType::Barracks:
    return QSize(3, 3);
  case BuildingType::Outpost:
    return QSize(1, 1);
  case BuildingType::PalisadeWall:
    return QSize(1, 1);
  case BuildingType::PalisadeGate:
    return QSize(1, 4);  // TODO: Make this rotatable.
  default:
    LOG(ERROR) << "Invalid type given: " << static_cast<int>(type);
    break;
  }
  
  return QSize(0, 0);
}

QRect GetBuildingOccupancy(BuildingType type) {
  if (type == BuildingType::TownCenter) {
    return QRect(0, 2, 2, 2);
  } else {
    QSize size = GetBuildingSize(type);
    return QRect(0, 0, size.width(), size.height());
  }
}

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
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingName() called on BuildingType::NumBuildings"; return "";
  }
  return "";
}

ResourceAmount GetBuildingCost(BuildingType type) {
  switch (type) {
  case BuildingType::TownCenter: return ResourceAmount(275, 0, 0, 100);
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterBack"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterCenter"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterFront"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterMain"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::House: return ResourceAmount(30, 0, 0, 0);
  
  case BuildingType::Mill: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::MiningCamp: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::LumberCamp: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::Dock: return ResourceAmount(150, 0, 0, 0);
  
  case BuildingType::Barracks: return ResourceAmount(175, 0, 0, 0);
  case BuildingType::Outpost: return ResourceAmount(25, 0, 0, 5);
  case BuildingType::PalisadeWall: return ResourceAmount(1, 0, 0, 0);
  case BuildingType::PalisadeGate: return ResourceAmount(5, 0, 0, 0);
  
  case BuildingType::TreeOak: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TreeOak"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingCost() called on BuildingType::NumBuildings"; return ResourceAmount(0, 0, 0, 0);
  }
  return ResourceAmount(0, 0, 0, 0);
}
