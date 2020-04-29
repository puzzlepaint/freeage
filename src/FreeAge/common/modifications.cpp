// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/modifications.hpp"

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

bool ObjectFilter::MatchesTechnology(const Player& player, const Technology& technology) const {
  switch(type) {
  case ObjectFilterType::AllTechnologies: 
    return true;
  case ObjectFilterType::TechnologyByType: 
    return data == static_cast<int>(technology);
  default: 
    return false;
  }
}