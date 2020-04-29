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
class ObjectTypeStats;
class UnitTypeStats;
class BuildingTypeStats;
class TechnologyStats;
class CivilizationStats;

/// NOTE: The Modification and ObjectFilter structs use mainly enums to to specify their
///       behavior instead of virtual methods and inheritance in order to allow (in the
///       future) a simple way of storing and loading them to binary files and/or text
///       text based formats.

/// A modification type describe what is the terget of the modification.
/// Some modification types expect specific values at the Data and Extra fields
/// of the modification.
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
    WorkRate,
    Resources,

    // Unit only
    Speed,
    ProductionTime,

    // Building only
    ConstructionTime,
    PopulationSpace,

    // Technology only
    ResearchDuration,
    TechnologyAvailability,    // Data: TechnologyAvailability (only Set)

    // civilization only
    PopulationMax,
    FreePopulationSpace,
    VillagerCarryingCapacity,  // Extra: villager type or Unset for all of them
    MonkHealRate,
    // TODO: add all modifiable monk conversion stats #monks

    // Special handling
    // TODO: Special handling #dependencies
    Upgrade,
};

enum class ModificationOperation {
    /// Set to the given value.
    Set,
    /// Add the given value.
    Add,
    /// Multiply the given value with the base value and add the result to the current value.
    /// The given value is divided by 100 before the calculation of the result.
    /// Examples:
    /// - current: 4, base: 4, given value: 20 => 4 + 4 * 20 /100 = 4.8
    //    which is +20% increase from current and base.
    /// - current: 4.8, base: 4, given value: 20 => 4.8 + 4 * 20 /100 = 5.6
    //    which is +20% increase from current but +44% from base.
    MultAdd,
};

/// A modifications to a stat of a unit, building, technology or civilization. The target of the modification
/// is described by a ModificationType, the type of operation by the ModificationOperation and there two more 
/// values, data and extra, that used based on the combination of the type and the operation. In most cases the
/// data value is used to specify the numerical value needed by the ModificationOperation and the extra value 
/// to specify the sub target of the target provided by the ModificationType. // TODO: rewrite?
struct Modification {

  inline Modification(ModificationType type, ModificationOperation operation, int value)
      : Modification(type, operation, value, Unset) {}

  inline Modification(ModificationType type, ModificationOperation operation, int value, DamageType damageType)
      : Modification(type, operation, value, static_cast<int>(damageType)) {
    CHECK(type == ModificationType::Damage || type == ModificationType::Armor);
  }

  inline Modification(ModificationType type, ModificationOperation operation, int value, ResourceType resourceType)
      : Modification(type, operation, value, static_cast<int>(resourceType)) {
    CHECK(type == ModificationType::Cost || type == ModificationType::Resources);
  }

  inline Modification(ModificationType type, ModificationOperation operation, TechnologyAvailability availability)
      : Modification(type, operation, static_cast<int>(availability), Unset) {
    CHECK(type == ModificationType::TechnologyAvailability);
  }

  bool ApplyToUnit(UnitTypeStats& stats, const UnitTypeStats& baseStats) const;
  bool ApplyToBuilding(BuildingTypeStats& stats, const BuildingTypeStats& baseStats) const;
  bool ApplyToTechnology(TechnologyStats& stats, const TechnologyStats& baseStats) const;
  bool ApplyToCivilization(CivilizationStats& stats, const CivilizationStats& baseStats) const;

 private:

  static constexpr i32 Unset = std::numeric_limits<i32>::min();

  bool ApplyToObject(ObjectTypeStats& stats, const ObjectTypeStats& baseStats) const;

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

  int CalculateInt(int current, int base) const;
  float CalculateFloat(float current, float base) const;
  void CalculateResourceAmount(ResourceAmount& resources, const ResourceAmount& baseResources) const;

};

// TODO: hide this enum inside the private scope of ObjectFilter?
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
  AllTechnologies, // except ages
  TechnologyByType,
};

/// A set of conditions for a unit, building, technology or civilization that can be used
/// to specify inderectly a collection of them.
///
/// Example: This following filter matches only with units that have the Cavalry armor class.
///    ObjectFilter filter = ObjectFilter::UnitsByArmor(DamageType::Cavalry);
///
/// An object filter can be created only with one of the factory methods.
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

/// A immutable pair of a filter and a modification. The modification should be able to apply to
/// the objects that are matched by the filter.
struct TargetedModification {

  inline TargetedModification(ObjectFilter filter, Modification modification)
      : filter(filter),
        modification(modification) {}

  const ObjectFilter filter;
  const Modification modification;

};
