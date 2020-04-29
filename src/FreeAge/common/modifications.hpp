// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/resources.hpp"
#include "FreeAge/common/technologies.hpp"
#include "FreeAge/common/unit_types.hpp"

class Player;
class UnitTypeStats;
class BuildingTypeStats;
class TechnologyStats;

enum class ModificationType {
    // Unit, building and technology
    Cost,            // Extra: affected ResourceType or Unset for to all of them

    // Unit and building
    MaxHp,
    Damage,          // Extra: affected DamageType
    Armor,           // Extra: affected DamageType
    MinRange,
    MaxRange,
    FireRate,
    Accuracy,
    LineOfSight,
    GarrisonCapacity,

    // Unit only
    Speed,
    ProductionTime,

    // Building only
    ConstructionSpeed,
    WorkRate,
    PopulationSpace,

    // Technology only
    ResearchTime,
    TechnologyAvailability,    // Data: TechnologyAvailability (only Set)

    // civilization only
    PopulationCap,
    VillagerCarryingCapacity,  // Extra: villager type or Unset for all of them

    // TODO: add all modifiable monk conversion stats #monks
    MonkHealRate,

    // Special handling

    // TODO: If the s
    Upgrade,

};

enum class ModificationOperation {
    Set, // Set the value
    Add, // Add the value
    MultAdd, // Multiply the factor with the base value and add the result to the current value
};

struct Modification {

  static constexpr i32 Unset = std::numeric_limits<i32>::min();
  
  inline Modification(ModificationType type, ModificationOperation operation, int value)
      : Modification(type, operation, value, 0) {}
  
  inline Modification(ModificationType type, ModificationOperation operation, int value, DamageType damageType)
      : Modification(type, operation, value, static_cast<int>(damageType)) {
    CHECK(type == ModificationType::Damage || type == ModificationType::Armor);
  }
  
  inline Modification(ModificationType type, ModificationOperation operation, TechnologyAvailability availability)
      : Modification(type, operation, static_cast<int>(availability), 0) {
    CHECK(type == ModificationType::TechnologyAvailability);
  }

  void ApplyToUnit(UnitTypeStats& stats, const UnitTypeStats& baseStats) const;
  void ApplyToBuilding(BuildingTypeStats& stats, const BuildingTypeStats& baseStats) const;
  void ApplyToTechnology(TechnologyStats& stats, const TechnologyStats& baseStats) const;

 private:
  
  inline Modification(ModificationType type, ModificationOperation operation, int value, int extra)
      : type(type),
        operation(operation),
        value(value),
        extra(extra) {}

  const ModificationType type;
  const ModificationOperation operation;
  
  /// Divide with 100.f in case of Mult
  const int value; 
  const int extra;

};

enum class ObjectFilterType {
  Civilization = 0,

  // Units
  AllUnits,
  UnitByType,
  UnitsByArmor,

  // Buildings
  AllBuildings,
  AllBuildingsExceptDefences,
  BuildingByType,
  BuildingsByArmor,

  // Techonolies
  AllTechnologies,
  TechnologyByType,
};

struct ObjectFilter {

  static const ObjectFilter Civilization() {
    return ObjectFilter(ObjectFilterType::Civilization, 0);
  }
  static const ObjectFilter AllUnits() {
    return ObjectFilter(ObjectFilterType::AllUnits, 0);
  }
  static const ObjectFilter UnitByType(UnitType unitType) {
    return ObjectFilter(ObjectFilterType::UnitByType, static_cast<int>(unitType));
  }
  static const ObjectFilter UnitsByArmor(DamageType damageType) {
    return ObjectFilter(ObjectFilterType::UnitsByArmor, static_cast<int>(damageType));
  }
  static const ObjectFilter AllBuildings() {
    return ObjectFilter(ObjectFilterType::AllBuildings, 0);
  }
  static const ObjectFilter AllBuildingsExceptDefences() {
    return ObjectFilter(ObjectFilterType::AllBuildingsExceptDefences, 0);
  }
  static const ObjectFilter BuildingsByType(BuildingType buildingType) {
    return ObjectFilter(ObjectFilterType::BuildingByType, static_cast<int>(buildingType));
  }
  static const ObjectFilter BuildingsByArmor(DamageType damageType) {
    return ObjectFilter(ObjectFilterType::BuildingsByArmor, static_cast<int>(damageType));
  }
  static const ObjectFilter AllTechnologies() {
    return ObjectFilter(ObjectFilterType::AllTechnologies, 0);
  }
  static const ObjectFilter TechnologyByType(Technology technology) {
    return ObjectFilter(ObjectFilterType::TechnologyByType, static_cast<int>(technology));
  }

  bool MatchesCivilization() const;
  bool MatchesUnits() const;
  bool MatchesBuildings() const;
  bool MatchesTechnologies() const;

  bool MatchesUnit(const Player& player, const UnitType& unitType) const;
  bool MatchesBuilding(const Player& player, const BuildingType& buildingType) const;
  bool MatchesTechnology(const Player& player, const Technology& technology) const;

 private:

  ObjectFilter(ObjectFilterType type, int data)
      : type(type),
        data(data) {};

  const ObjectFilterType type;
  
  // TODO: use union ?
  const int data;

};

// TODO: rename ?
struct TargetedModification {
  
  inline TargetedModification(ObjectFilter filter, Modification modification)
      : filter(filter),
        modification(modification) {}

  const ObjectFilter filter;
  const Modification modification;

};
