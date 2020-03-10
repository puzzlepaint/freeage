#pragma once

/// Unit types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector.
enum class UnitType {
  FemaleVillager,
  MaleVillager,
  Scout,
  
  NumUnits
};

float GetUnitRadius(UnitType type);
