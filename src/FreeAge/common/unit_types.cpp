#include "FreeAge/common/unit_types.hpp"

#include <QObject>

#include "FreeAge/common/logging.hpp"

float GetUnitRadius(UnitType type) {
  // TODO: Load this from some data file
  switch (type) {
  case UnitType::FemaleVillager:
  case UnitType::MaleVillager:
    return 0.15;
  case UnitType::Scout:
    return 0.3;
  case UnitType::NumUnits:
    LOG(ERROR) << "GetUnitRadius() called on UnitType::NumUnits";
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
  switch (type) {
  case UnitType::FemaleVillager:
  case UnitType::MaleVillager:
    return ResourceAmount(0, 50, 0, 0);
  case UnitType::Scout:
    return ResourceAmount(0, 80, 0, 0);
  case UnitType::NumUnits:
    LOG(ERROR) << "GetUnitName() called on UnitType::NumUnits";
    break;
  }
  
  return ResourceAmount(0, 0, 0, 0);
}

float GetUnitProductionTime(UnitType type) {
  // TODO: Load this from some data file
  switch (type) {
  case UnitType::FemaleVillager:
  case UnitType::MaleVillager:
    return 1;  // TODO: Set too short on purpose for testing
  case UnitType::Scout:
    return 4;  // TODO: Set too short on purpose for testing
  case UnitType::NumUnits:
    LOG(ERROR) << "GetUnitProductionTime() called on UnitType::NumUnits";
    break;
  }
  
  return -1;
}
