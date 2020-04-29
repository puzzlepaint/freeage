// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/modifications.hpp"
#include "FreeAge/common/resources.hpp"
#include "FreeAge/common/technologies.hpp"
#include "FreeAge/common/unit_types.hpp"

enum class Civilization {
  Gaia = 0,

  ReplaceWithPuzzlepaintsFavoriteCivilization,
  Byzantines,

  NumCivilizations
};

// static Civilization GetRandomPlayerCivilization() {
//   // skips the Gaia civilization
//   return static_cast<Civilization>((rand() % (static_cast<int>(Civilization::NumCivilizations) - 1) + 1));
// }

// TODO: Split CivilizationStats to other file, in order to be able to include only the Civilization enum to the 
//       pre game code.

constexpr int kNumVillagerTypes = (static_cast<int>(UnitType::LastVillager) - static_cast<int>(UnitType::FirstVillager)) / 2;

static int GetVillagerTypeIndex(UnitType unitType) {
    CHECK(IsVillager(unitType));
    return (static_cast<int>(unitType)- static_cast<int>(UnitType::FirstVillager)) % kNumVillagerTypes;
}

struct CivilizationStats {

  // Game start information

  /// The starting scout unit type.
  UnitType startingScoutUnit;

  /// The starting number of villagers.
  int startingVillagerCount;

  /// TODO: not used by the game yet
  bool startingLlama;

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
  float tradingFree;

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

  /// TODO: not used by the game yet
  int villagerCarryingCapacity[kNumVillagerTypes];

  inline int& VillagerCarryingCapacity(UnitType villagerType) { return villagerCarryingCapacity[GetVillagerTypeIndex(villagerType)];  }

  /// TODO: not used by the game yet
  int fishingShipCarryingCapacity;

  // TODO: all modifiable monk conversion stats #monks

  // TODO (maanoo): doc
  std::vector<TargetedModification> ageModifications[static_cast<int>(Technology::NumAges)];

  inline std::vector<TargetedModification>& modifications(Technology age) {
    CHECK(IsAge(age));
    return ageModifications[static_cast<int>(age)];
  }

  // TODO (maanoo): doc
  std::vector<TargetedModification> teamModifications;

};

