#include "FreeAge/common/unit_types.hpp"

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
