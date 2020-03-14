#pragma once

#include <QString>

#include "FreeAge/common/resources.hpp"

/// Unit types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector.
enum class UnitType {
  FemaleVillager,
  MaleVillager,
  Scout,
  
  NumUnits
};

float GetUnitRadius(UnitType type);
QString GetUnitName(UnitType type);

/// TODO: This needs to consider the player's civilization and researched technologies
ResourceAmount GetUnitCost(UnitType type);

/// Returns the production time for the unit in seconds.
/// TODO: This needs to consider the player's civilization and researched technologies
float GetUnitProductionTime(UnitType type);
