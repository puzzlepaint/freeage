// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/logging.hpp"

#include <algorithm>

DamageValues::DamageValues() {
  for (int i = 0; i < static_cast<int>(DamageType::NumDamageTypes); ++ i) {
    values[i] = None;
  }
}

DamageValues::DamageValues(const DamageValues& other) {
  for (int i = 0; i < static_cast<int>(DamageType::NumDamageTypes); ++ i) {
    values[i] = other.values[i];
  }
}

void DamageValues::AddValue(DamageType damageType, i32 value) {
  if (value == None) {
    return; // ignore
  }
  i32 baseValue = GetValue(damageType);
  if (baseValue == None) {
    SetValue(damageType, value);
  } else {
    // add values only if they are both != None
    SetValue(damageType, baseValue + value);
  }
}

Armor GetDefaultArmor(bool isUnit) {
  Armor armor;
  if (isUnit) {
    // NOTE: Inserting this CHECK_EQ() was necessary for the call
    //       armor.SetValue(DamageType::Melee, 0) below to yield the expected result
    //       when compiled with MSVC. This seems like a possible compiler bug.
    CHECK_EQ(armor.GetValue(DamageType::Melee), Armor::None);
    armor.SetValue(DamageType::Melee, 0);
    armor.SetValue(DamageType::Pierce, 0);
    armor.SetValue(DamageType::AntiLeitis, 0);
    CHECK_EQ(armor.GetValue(DamageType::Melee), 0);
  }
  return armor;
}

Damage GetDefaultDamage(bool /*isUnit*/) {
  Damage damage;
  return damage;
}

i32 CalculateDamage(const Damage& damage, const Armor& armor, float multiplier) {
  i32 sum = 0;
  for (int i = 0; i < static_cast<int>(DamageType::NumDamageTypes); ++ i) {
    i32 armorValue = armor.GetValue(i);
    if (armorValue == Armor::None) {
      // the defending unit does not take damage from this damage type
      continue;
    }
    i32 damageValue = damage.GetValue(i);
    if (damageValue == Damage::None) {
      // the attacking unit does not deal damage of this damage type
      continue;
    }
    // NOTE: damageValue can be 0, armorValue can be negative
    sum += std::max(0, damageValue - armorValue);
  }
  // damage is always at least 1
  return std::max(1, static_cast<i32>(sum * multiplier + .5f));
}
