#include "FreeAge/common/unit_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"

float GetUnitRadius(UnitType type) {
  // TODO: Load this from some data file
  
  if (IsVillager(type)) {
    return 0.15;
  }
  
  switch (type) {
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
    return 1;  // TODO: Set too short on purpose for testing
  }
  
  switch (type) {
  case UnitType::Scout:
    return 4;  // TODO: Set too short on purpose for testing
  default:
    LOG(ERROR) << "GetUnitProductionTime() called on unsupported type: " << static_cast<int>(type);
    break;
  }
  
  return -1;
}
