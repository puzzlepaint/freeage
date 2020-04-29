// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/modifications.hpp"

#include "FreeAge/common/civilizations.hpp"
#include "FreeAge/common/player.hpp"
#include "FreeAge/common/type_stats.hpp"

#define CalculateCaseI(type, field) case type: stats.field = CalculateInt(stats.field, baseStats.field); return true;
#define CalculateCaseF(type, field) case type: stats.field = CalculateFloat(stats.field, baseStats.field); return true;
#define CalculateCaseR(type, field) case type: CalculateResourceAmount(stats.field, baseStats.field); return true;

bool Modification::ApplyToUnit(UnitTypeStats& stats, const UnitTypeStats& baseStats) const {
  if (ApplyToObject(stats, baseStats)) {
    return true;
  }
  switch(type) {
  CalculateCaseF(ModificationType::Speed, creationTime);
  CalculateCaseF(ModificationType::ProductionTime, creationTime);
  default:
    LOG(ERROR) << "Modification type cannot be applied to unit: " << static_cast<int>(type);
    return false;
  }
}

bool Modification::ApplyToBuilding(BuildingTypeStats& stats, const BuildingTypeStats& baseStats) const {
  if (ApplyToObject(stats, baseStats)) {
    return true;
  }
  switch(type) {
  CalculateCaseF(ModificationType::ConstructionTime, creationTime);

  case ModificationType::PopulationSpace:
    stats.population.SetToIntegerPopulationSpace(CalculateInt(stats.population.GetPopulationSpace(),
        baseStats.population.GetPopulationSpace()));
    return true;
  default:
    LOG(ERROR) << "Modification type cannot be applied to building: " << static_cast<int>(type);
    return false;
  }
}

bool Modification::ApplyToObject(ObjectTypeStats& stats, const ObjectTypeStats& baseStats) const {
  switch(type) {
  CalculateCaseI(ModificationType::MaxHp, maxHp);
  CalculateCaseI(ModificationType::MinRange, minRange);
  CalculateCaseI(ModificationType::MaxRange, maxRange);
  CalculateCaseF(ModificationType::Accuracy, accuracy);
  CalculateCaseF(ModificationType::LineOfSight, lineOfSight);
  CalculateCaseI(ModificationType::GarrisonCapacity, garrisonCapacity);
  CalculateCaseF(ModificationType::WorkRate, workRate);
  CalculateCaseR(ModificationType::Cost, cost);

  case ModificationType::Damage: {
    DamageType damageType = static_cast<DamageType>(extra);
    int current = stats.damage.GetValue(damageType);
    int newValue; 
    if (current == DamageValues::None || value == DamageValues::None) {
      newValue = value;
    } else {
      int base = baseStats.damage.GetValue(damageType);
      newValue = CalculateInt(current, base);
    }
    stats.damage.SetValue(damageType, newValue);
    return true;
  }

  case ModificationType::Armor: {
    DamageType damageType = static_cast<DamageType>(extra);
    int current = stats.armor.GetValue(damageType);
    int newValue; 
    if (current == DamageValues::None || value == DamageValues::None) {
      newValue = value;
    } else {
      int base = baseStats.armor.GetValue(damageType);
      newValue = CalculateInt(current, base);
    }
    stats.armor.SetValue(damageType, newValue);
    return true;
  }

  case ModificationType::FireRate: {
    // TODO: revisit!!
    if (operation != ModificationOperation::MultAdd) {
      stats.fireRate = CalculateFloat(stats.fireRate, baseStats.fireRate); 
      return true;
    }
    int current = stats.fireRate;
    int base = baseStats.fireRate;
    float newValue;
    if (current == 0 || base == 0) {
      newValue = 0;
    } else {
      // float multiplier = 1; // Proper formula
      float multiplier = (1 / (1 - (value / 100.f)) - 1); // Original game formula
      newValue = 1/((1/current) + (1/base) * multiplier);
    }
    stats.fireRate = newValue;
    return true;
  }
  default:
    return false;
  }
}

bool Modification::ApplyToTechnology(TechnologyStats& stats, const TechnologyStats& baseStats) const {
  switch(type) {
  CalculateCaseF(ModificationType::ResearchDuration, researchDuration);
  CalculateCaseR(ModificationType::Cost, cost);

  case ModificationType::TechnologyAvailability: {
    if (operation != ModificationOperation::Set) {
      LOG(ERROR) << "Modification type only supports Set operation: " << static_cast<int>(operation);
      return false;
    }
    stats.availability = static_cast<TechnologyAvailability>(value);
    return true;
  }
  default:
    LOG(ERROR) << "Modification type cannot be applied to technology: " << static_cast<int>(type);
    return false;
  }
}

bool Modification::ApplyToCivilization(CivilizationStats& stats, const CivilizationStats& baseStats) const {
  switch(type) {
  CalculateCaseI(ModificationType::PopulationMax, bonusMaxPopulation);
  CalculateCaseI(ModificationType::FreePopulationSpace, bonusPopulationSpace);
  CalculateCaseI(ModificationType::MonkHealRate, monkHealRate);
  // TODO (maanoo): implement VillagerCarryingCapacity
  default:
    LOG(ERROR) << "Modification type cannot be applied to civilization: " << static_cast<int>(type);
    return false;
  }
}

#undef CalculateCaseI
#undef CalculateCaseF

int Modification::CalculateInt(int current, int base) const {
  switch(operation) {
  case ModificationOperation::Set: return value;
  case ModificationOperation::Add: return current + value;
  case ModificationOperation::MultAdd: return current + base * value / 100.0f;
  }
  LOG(ERROR) << "Unknown modification operation: " << static_cast<int>(operation);
  return value;
}

float Modification::CalculateFloat(float current, float base) const {
  switch(operation) {
  case ModificationOperation::Set: return value;
  case ModificationOperation::Add: return current + value;
  case ModificationOperation::MultAdd: return current + base * value / 100.0f;
  }
  LOG(ERROR) << "Unknown modification operation: " << static_cast<int>(operation);
  return value;
}

void Modification::CalculateResourceAmount(ResourceAmount& resources, const ResourceAmount& baseResources) const {
  if (extra == Unset) {
    // apply effect to all resources
    for (int index = 0; index < static_cast<int>(ResourceType::NumTypes); ++ index) {
      float value = CalculateFloat(resources.resources[index], baseResources.resources[index]);
      resources.resources[index] = static_cast<u32>(std::max<float>(0, value + .5f));
    }
  } else {
    // apply effect to the resource specifies by the extra variable
    int index = extra;
    float value = CalculateFloat(resources.resources[index], baseResources.resources[index]);
    resources.resources[index] = static_cast<u32>(std::max<float>(0, value + .5f));
  }
}

// ObjectFilter

bool ObjectFilter::MatchesCivilization() const {
  return type == ObjectFilterType::Civilization;
}

bool ObjectFilter::MatchesUnits() const {
  return type == ObjectFilterType::AllUnits ||
         type == ObjectFilterType::UnitByType ||
         type == ObjectFilterType::UnitsByArmor;
}

bool ObjectFilter::MatchesBuildings() const {
  return type == ObjectFilterType::AllBuildings ||
         type == ObjectFilterType::AllBuildingsExceptDefences ||
         type == ObjectFilterType::BuildingByType ||
         type == ObjectFilterType::BuildingsByArmor;
}

bool ObjectFilter::MatchesTechnologies() const {
  return type == ObjectFilterType::AllTechnologies ||
         type == ObjectFilterType::TechnologyByType;
}

bool ObjectFilter::MatchesUnit(const Player& player, const UnitType& unitType) const {
  switch(type) {
  case ObjectFilterType::AllUnits: 
    return true;
  case ObjectFilterType::UnitByType: 
    return data == static_cast<int>(unitType);
  case ObjectFilterType::UnitsByArmor:
    return player.GetUnitStats(unitType).armor.HasValue(static_cast<DamageType>(data));
  default: 
    return false;
  }
}

bool ObjectFilter::MatchesBuilding(const Player& player, const BuildingType& buildingType) const {
  switch(type) {
  case ObjectFilterType::AllBuildings: 
    return true;
  case ObjectFilterType::AllBuildingsExceptDefences: 
    // All Buildings except Outposts, Towers, Castles and Walls
    // TODO: complete list #on-new-building
    return buildingType != BuildingType::Outpost &&
           buildingType != BuildingType::PalisadeWall &&
           buildingType != BuildingType::PalisadeGate;
  case ObjectFilterType::BuildingByType: 
    return data == static_cast<int>(buildingType);
  case ObjectFilterType::BuildingsByArmor:
    return player.GetBuildingStats(buildingType).armor.HasValue(static_cast<DamageType>(data));
  default: 
    return false;
  }
}

bool ObjectFilter::MatchesTechnology(const Player& /*player*/, const Technology& technology) const {
  switch(type) {
  case ObjectFilterType::AllTechnologies: 
    return !IsAge(technology);
  case ObjectFilterType::TechnologyByType: 
    return data == static_cast<int>(technology);
  default: 
    return false;
  }
}