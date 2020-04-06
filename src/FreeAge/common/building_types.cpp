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
  case BuildingType::ForageBush:
    return QSize(1, 1);
  case BuildingType::GoldMine:
    return QSize(1, 1);
  case BuildingType::StoneMine:
    return QSize(1, 1);
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
  case BuildingType::ForageBush: return QObject::tr("Forage Bush");
  case BuildingType::GoldMine: return QObject::tr("Gold Mine");
  case BuildingType::StoneMine: return QObject::tr("Stone Mine");
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingName() called on BuildingType::NumBuildings"; return "";
  }
  return "";
}

double GetBuildingConstructionTime(BuildingType type) {
  // TODO: These values are chosen arbitrarily (and small for testing). Use the correct values.
  
  switch (type) {
  case BuildingType::TownCenter: return 10;
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::TownCenterBack"; return 0;
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::TownCenterCenter"; return 0;
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::TownCenterFront"; return 0;
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::TownCenterMain"; return 0;
  case BuildingType::House: return 3;
  
  case BuildingType::Mill: return 5;
  case BuildingType::MiningCamp: return 5;
  case BuildingType::LumberCamp: return 5;
  case BuildingType::Dock: return 7;
  
  case BuildingType::Barracks: return 7;
  case BuildingType::Outpost: return 2;
  case BuildingType::PalisadeWall: return 1;
  case BuildingType::PalisadeGate: return 3;
  
  case BuildingType::TreeOak: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::TreeOak"; return 0;
  case BuildingType::ForageBush: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::ForageBush"; return 0;
  case BuildingType::GoldMine: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::GoldMine"; return 0;
  case BuildingType::StoneMine: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::StoneMine"; return 0;
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingConstructionTime() called on BuildingType::NumBuildings"; return 0;
  }
  return 0;
}

ResourceAmount GetBuildingCost(BuildingType type) {
  switch (type) {
  case BuildingType::TownCenter: return ResourceAmount(275, 0, 0, 100);
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterBack"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterCenter"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterFront"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TownCenterMain"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::House: return ResourceAmount(25, 0, 0, 0);
  
  case BuildingType::Mill: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::MiningCamp: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::LumberCamp: return ResourceAmount(100, 0, 0, 0);
  case BuildingType::Dock: return ResourceAmount(150, 0, 0, 0);
  
  case BuildingType::Barracks: return ResourceAmount(175, 0, 0, 0);
  case BuildingType::Outpost: return ResourceAmount(25, 0, 0, 5);
  case BuildingType::PalisadeWall: return ResourceAmount(1, 0, 0, 0);
  case BuildingType::PalisadeGate: return ResourceAmount(5, 0, 0, 0);
  
  case BuildingType::TreeOak: LOG(ERROR) << "GetBuildingCost() called on BuildingType::TreeOak"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::ForageBush: LOG(ERROR) << "GetBuildingCost() called on BuildingType::ForageBush"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::GoldMine: LOG(ERROR) << "GetBuildingCost() called on BuildingType::GoldMine"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::StoneMine: LOG(ERROR) << "GetBuildingCost() called on BuildingType::StoneMine"; return ResourceAmount(0, 0, 0, 0);
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingCost() called on BuildingType::NumBuildings"; return ResourceAmount(0, 0, 0, 0);
  }
  return ResourceAmount(0, 0, 0, 0);
}

bool IsDropOffPointForResource(BuildingType building, ResourceType resource) {
  if (building == BuildingType::TownCenter) {
    return true;
  } else if (building == BuildingType::LumberCamp &&
             resource == ResourceType::Wood) {
    return true;
  } else if (building == BuildingType::MiningCamp &&
             (resource == ResourceType::Gold || resource == ResourceType::Stone)) {
    return true;
  } else if (building == BuildingType::Mill &&
             resource == ResourceType::Food) {
    return true;
  }
  return false;
}

u32 GetBuildingMaxHP(BuildingType type) {
  switch (type) {
  case BuildingType::TownCenter: return 2400;
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingMaxHP() called on BuildingType::TownCenterBack"; return 0;
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingMaxHP() called on BuildingType::TownCenterCenter"; return 0;
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingMaxHP() called on BuildingType::TownCenterFront"; return 0;
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingMaxHP() called on BuildingType::TownCenterMain"; return 0;
  case BuildingType::House: return 550;
  
  case BuildingType::Mill: return 600;
  case BuildingType::MiningCamp: return 600;
  case BuildingType::LumberCamp: return 600;
  case BuildingType::Dock: return 1800;
  
  case BuildingType::Barracks: return 1200;
  case BuildingType::Outpost: return 500;
  case BuildingType::PalisadeWall: return 250;
  case BuildingType::PalisadeGate: return 400;
  
  case BuildingType::TreeOak: return 0;
  case BuildingType::ForageBush: return 0;
  case BuildingType::GoldMine: return 0;
  case BuildingType::StoneMine: return 0;
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingMaxHP() called on BuildingType::NumBuildings"; return 0;
  }
  return 0;
}

u32 GetBuildingMeleeArmor(BuildingType type) {
  switch (type) {
  case BuildingType::TownCenter: return 3;
  case BuildingType::TownCenterBack: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::TownCenterBack"; return 0;
  case BuildingType::TownCenterCenter: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::TownCenterCenter"; return 0;
  case BuildingType::TownCenterFront: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::TownCenterFront"; return 0;
  case BuildingType::TownCenterMain: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::TownCenterMain"; return 0;
  case BuildingType::House: return 0;
  
  case BuildingType::Mill: return 0;
  case BuildingType::MiningCamp: return 0;
  case BuildingType::LumberCamp: return 0;
  case BuildingType::Dock: return 0;
  
  case BuildingType::Barracks: return 0;
  case BuildingType::Outpost: return 0;
  case BuildingType::PalisadeWall: return 2;  // TODO: 0 during construction?
  case BuildingType::PalisadeGate: return 2;  // TODO: 0 during construction?
  
  case BuildingType::TreeOak: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::TreeOak"; return 0;
  case BuildingType::ForageBush: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::ForageBush"; return 0;
  case BuildingType::GoldMine: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::GoldMine"; return 0;
  case BuildingType::StoneMine: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::StoneMine"; return 0;
  case BuildingType::NumBuildings: LOG(ERROR) << "GetBuildingMeleeArmor() called on BuildingType::NumBuildings"; return 0;
  }
  return 0;
}

int GetBuildingProvidedPopulationSpace(BuildingType type) {
  if (type == BuildingType::House || type == BuildingType::TownCenter) {
    return 5;
  }
  return 0;
}
