// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/modifications.hpp"
#include "FreeAge/common/resources.hpp"
#include "FreeAge/common/technologies.hpp"
#include "FreeAge/common/unit_types.hpp"

enum class Civilization {
  Gaia = 0,

  Aztecs,
  Byzantines,

  NumCivilizations
};

constexpr Civilization kDefaultCivilization = Civilization::Aztecs;

// TODO: implement GetCivilizationName()

// static Civilization GetRandomPlayerCivilization() {
//   // skips the Gaia civilization
//   return static_cast<Civilization>((rand() % (static_cast<int>(Civilization::NumCivilizations) - 1) + 1));
// }

// TODO: Split CivilizationStats to other file, in order to be able to include only the Civilization enum to the 
//       pre game code.


static int GetVillagerTypeIndex(UnitType unitType) {
    CHECK(IsVillager(unitType));
    return (static_cast<int>(unitType) - static_cast<int>(UnitType::FirstVillager)) % static_cast<int>(UnitType::NumVillagerTypes);
}

struct CivilizationStats {

  // Game start information

  /// The starting scout unit type.
  UnitType startingScoutUnit;

  /// The starting number of villagers.
  int startingVillagerCount;

  /// TODO: not used by the game yet
  bool startingLlama;

  /// Resources added the player stockpile after the map specific resources have been added.
  /// TODO: not used by the game yet
  ResourceAmount startingBonusResources;

  // Global information

  /// TODO: not used by the game yet
  int bonusMaxPopulation;

  /// TODO: not used by the game yet
  int bonusPopulationSpace;

  /// Regenerated HP per minute for garrisoned units. 
  /// TODO: handle special case: Castle heal twice as fast
  /// TODO: not used by the game yet
  float garrisonHealRate;
  
  /// TODO: not used by the game yet
  float tradingFee;

  /// TODO: not used by the game yet
  float tributeFee;
  
  /// TODO: not used by the game yet
  float tradeGoldGeneration;

  /// Gold generated form garrisoned relics per second.
  /// TODO: not used by the game yet
  float relicGoldGeneration;

  // Single unit/building information

  /// The upper limit on how many Town Centers can be alive at the same time.
  int maxTownCenters;

  /// The max carrying capacity for each type of villager.
  /// TODO: not used by the game yet
  int villagerCarryingCapacity[static_cast<int>(UnitType::NumVillagerTypes)];

  inline int& VillagerCarryingCapacity(UnitType villagerType) {
    CHECK(IsVillager(villagerType));
    return villagerCarryingCapacity[GetVillagerTypeIndex(villagerType)];
  }

  /// The max carrying capacity for fishing boat
  /// TODO: not used by the game yet
  int fishingShipCarryingCapacity;

  // TODO: all modifiable monk conversion stats #monks
  
  // Monks

  /// TODO: not used by the game yet
  float monkHealRate;

  // Modifications

  // The modifications to be added to the modifications of the corresponding age. For modifications that apply
  // from the start of the game the Dark age can be used.
  std::vector<TargetedModification> ageModifications[static_cast<int>(Technology::NumAges)];

  inline std::vector<TargetedModification>& modifications(Technology age) {
    CHECK(IsAge(age));
    return ageModifications[static_cast<int>(age)];
  }

  // The modifications that will apply to all allies at the start of the game.
  std::vector<TargetedModification> teamModifications;

};

