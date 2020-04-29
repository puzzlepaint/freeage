// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/player.hpp"

#include <cassert>

#include "FreeAge/common/modifications.hpp"

/// NOTE: In order to reduce the verbosity the player is passed with the constructor instead 
///       of with each function call, Passing the player pointer restricts the player object 
///       to not change the address.

PlayerStats::PlayerStats(const Player* player)
    : player(player) {
  // Initialize all stats
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    unitsAlive[unitType] = 0;
    unitsDied[unitType] = 0;
  }
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    buildingConstructions[buildingType] = 0;
    buildingAlive[buildingType] = 0;
    buildingExisted[buildingType] = false;
  }
}

PlayerStats::~PlayerStats() {
  // empty
}

// interface methods

void PlayerStats::BuildingAdded(BuildingType buildingType, bool finished) {
  if (finished) {
    FinishedBuildingChange(buildingType, 1);
  } else {
    UnfinishedBuildingChange(buildingType, 1);
  }
}

void PlayerStats::UnitAdded(UnitType unitType) {
  UnitChange(unitType, false, 1);
}

void PlayerStats::BuildingRemoved(BuildingType buildingType, bool finished) {
  if (finished) {
    FinishedBuildingChange(buildingType, -1);
  } else {
    UnfinishedBuildingChange(buildingType, -1);
  }
}

void PlayerStats::UnitRemoved(UnitType unitType) {
  UnitChange(unitType, true, -1);
}

void PlayerStats::BuildingTransformed(BuildingType fromBuildingType, BuildingType toBuildingType) {
  FinishedBuildingChange(fromBuildingType, -1);
  FinishedBuildingChange(toBuildingType, 1);
}
void PlayerStats::UnitTransformed(UnitType fromUnitType, UnitType toUnitType) {
  UnitChange(fromUnitType, false, -1);
  UnitChange(toUnitType, false, 1);
}

void PlayerStats::BuildingFinished(BuildingType buildingType) {
  UnfinishedBuildingChange(buildingType, -1);
  FinishedBuildingChange(buildingType, 1);
}

// core methods

void PlayerStats::UnfinishedBuildingChange(BuildingType buildingType, int d) {
  buildingConstructions[static_cast<int>(buildingType)] += d;
}

void PlayerStats::FinishedBuildingChange(BuildingType buildingType, int d) {
  const BuildingTypeStats& stats = player->GetBuildingStats(buildingType);

  buildingAlive[static_cast<int>(buildingType)] += d;
  if (d > 0) {
    buildingExisted[static_cast<int>(buildingType)] = true;
  }

  Change(stats, d);
}

void PlayerStats::UnitChange(UnitType unitType, bool death, int d) {
  assert(!death || d < 0); // death => d < 0
  const UnitTypeStats& stats = player->GetUnitStats(unitType);

  unitsAlive[static_cast<int>(unitType)] += d;
  if (death) {
    unitsDied[static_cast<int>(unitType)] += -d;
  }
  /// Keep track of classes of units.
  if (IsVillager(unitType)) {
    villagerCount += d;
  }

  Change(stats, d);
}

void PlayerStats::Change(const ObjectTypeStats& stats, int d) {
  // NOTE: This is not enforced at the moment, but there are no technologies that can be researched
  //       that change the population value of an object. So it can be assumed that the two counters
  //       will not be invalidated at any point, no total re-evaluations are needed.
  // Keep track of the population.
  doubledPopulationCount += stats.population.GetDoubledPopulationCount() * d;
  doubledPopulationSpace += stats.population.GetDoubledPopulationSpace() * d;
}

void PlayerStats::log() const {
  LOG(INFO) << "--- Stats";
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    if (!unitsAlive[unitType] && !unitsDied[unitType]) {
      continue;
    }
    LOG(INFO) << GetUnitName(static_cast<UnitType>(unitType)).toStdString() << "(" << unitType << ")"
        << " " << unitsAlive[unitType]
        << " " << unitsDied[unitType];
  }
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    if (!buildingExisted[buildingType] && !buildingAlive[buildingType] && !buildingConstructions[buildingType]) {
      continue;
    }
    LOG(INFO) << GetBuildingName(static_cast<BuildingType>(buildingType)).toStdString() << "(" << buildingType << ")"
        << " " << buildingExisted[buildingType]
        << " " << buildingAlive[buildingType]
        << " " << buildingConstructions[buildingType];
  }
}

Player::Player(int index, int playerColorIndex, const GameData& gameData, Civilization civilization)
  : index(index),
    playerColorIndex(playerColorIndex),
    civilization(civilization),
    civilizationStats(gameData.civilizationStats[static_cast<int>(civilization)]),
    unitTypeStats(gameData.unitTypeStats),
    buildingTypeStats(gameData.buildingTypeStats),
    technologyStats(gameData.technologyStats),
    stats(this) {
  // Apply the civ bonuses to the age technologies
  for (int age = 0; age < static_cast<int>(Technology::NumAges); ++ age) {
    for (auto& modification : civilizationStats.modifications(static_cast<Technology>(age))) {
      technologyStats.at(age).modifications.push_back(modification);
    }
  }
}

void Player::ApplyTechnologyModifications(Technology technology, const Player& basePlayer) {
  TechnologyStats& stats = technologyStats.at(static_cast<int>(technology));
  for (auto& targetedModification : stats.modifications) {
    ApplyModification(targetedModification, basePlayer);
  }
  LOG(INFO) << "ApplyTechnologyModifications performed " << stats.modifications.size() << " ApplyModifications";
  // TODO: store that the technology have been researched
}

void Player::ApplyModification(const TargetedModification& targetedModification, const Player& basePlayer) {
  const ObjectFilter& filter = targetedModification.filter;
  const Modification& modification = targetedModification.modification;
  int changes = 0;
  if (filter.MatchesUnits()) {
    for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
      if (filter.MatchesUnit(*this, static_cast<UnitType>(unitType)) &&
          modification.ApplyToUnit(unitTypeStats.at(unitType), basePlayer.unitTypeStats.at(unitType))) {
        ++ changes;
      }
    }
  }
  if (filter.MatchesBuildings()) {
    for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
      if (filter.MatchesBuilding(*this, static_cast<BuildingType>(buildingType)) &&
          modification.ApplyToBuilding(buildingTypeStats.at(buildingType), basePlayer.buildingTypeStats.at(buildingType))) {
        ++ changes;
      }
    }
  }
  if (filter.MatchesTechnologies()) {
    for (int technology = 0; technology < static_cast<int>(Technology::NumTechnologies); ++ technology) {
      if (filter.MatchesTechnology(*this, static_cast<Technology>(technology)) &&
          modification.ApplyToTechnology(technologyStats.at(technology), basePlayer.technologyStats.at(technology))) {
        ++ changes;
      }
    }
  }
  if (filter.MatchesCivilization()) {
    modification.ApplyToCivilization(civilizationStats, basePlayer.civilizationStats);
  }
  LOG(1) << "ApplyModification caused " << changes << " changes";
}