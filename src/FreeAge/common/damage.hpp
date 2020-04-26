// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <limits>

#include "FreeAge/common/free_age.hpp"

enum class DamageType {
  Melee = 0,
  Pierce,
  // units
  Infantry,
  TurtleShip,
  WarElephant,
  Cavalry,
  PredatorAnimals,
  Archer,
  Ship,
  Ram,
  Tree,
  UniqueUnit,
  SiegeWeapon,
  GunpowderUnit,
  Boar,
  Monk,
  Spearman,
  CavalryArcher,
  EagleWarrior,
  Camel,
  AntiLeitis,
  Condottiero,
  FishingShip,
  Mameluke,
  HeroKing,
  Villager, // Not in the original 
  // buildings
  Building,
  StoneDefense,
  StandardBuilding,
  WallGate,
  Castle,

  NumDamageTypes
};

/// TODO: Reorder the damage types in way that make sense and even could be used to compress
///       the DamageValues when is send over the network or stored. Note that the original game
///       ids are not 100% consecutive.

/// A map of every DamageType to a signed integer.
/// It can represent the damage or the armor of a unit or a building. The special value
/// DamageValues::None is used to define that the corresponding DamageType is ignored.
struct DamageValues {

  /// Defines that the corresponding DamageType is ignored.
  static constexpr i32 None = std::numeric_limits<i32>::min();

  /// Initialize all values with None.
  /// Prefer GetDefaultArmor and GetDefaultDamage to create new objects.
  DamageValues();

  DamageValues(const DamageValues& other);

  inline constexpr DamageValues& operator = (const DamageValues& other) {
    for (int i = 0; i < static_cast<int>(DamageType::NumDamageTypes); ++ i) {
      values[i] = other.values[i];
    }
    return *this;
  }

  /// Increase the value of a damage type (handling cases with values of None).
  void AddValue(DamageType damageType, i32 value);

  void SetValue(DamageType damageType, i32 value) { values[static_cast<int>(damageType)] = value; }

  i32 GetValue(DamageType damageType) const { return values[static_cast<int>(damageType)]; }
  i32 GetValue(int index) const { return values[index]; }

  i32 HasValue(DamageType damageType) const { return values[static_cast<int>(damageType)] != None; }

  // The original gui mostly uses the Melee and Pierce values.

  i32 melee() const { return GetValue(DamageType::Melee); }
  i32 pierce() const { return GetValue(DamageType::Pierce); }

 private:

  /// A value for each damage type.
  /// TODO: reduce to i16 ?, max value of original game is 250
  i32 values[static_cast<int>(DamageType::NumDamageTypes)];

};

/// DamageValues can be used for the damage or the armor of a unit or a building.
/// To make the code clearer the two typedefs Damage and Armor should be used.

// The armor of a unit or a building.
typedef DamageValues Armor;
/// The attack damage of a unit or a building.
typedef DamageValues Damage;

/// Returns the Damage with the default values.
Armor GetDefaultArmor(bool isUnit);
/// Returns the Damage with the default values.
Damage GetDefaultDamage(bool isUnit);

inline Armor GetUnitDefaultArmor() { return GetDefaultArmor(true); }
inline Damage GetUnitDefaultDamage() { return GetDefaultDamage(true); }
inline Armor GetBuildingDefaultArmor() { return GetDefaultArmor(false); }
inline Damage GetBuildingDefaultDamage() { return GetDefaultDamage(false); }

/// Returns the damage (reduction of health) that the given Damage will cause to the
/// object with the given Armor.
i32 CalculateDamage(const Damage& damage, const Armor& armor, float multiplier = 1);
