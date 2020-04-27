// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/player.hpp"

#include <cassert>

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
  // Keep track of the population. The population values can only change in the start of the 
  // game, so no total re-evaluations are needed.
  doubledPopulationDemand += stats.population.GetDoubledPopulationDemand() * d;
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

Player::Player(int index, int playerColorIndex, const GameData& gameData) 
  : index(index),
    playerColorIndex(playerColorIndex),
    unitTypeStats(gameData.unitTypeStats),
    buildingTypeStats(gameData.buildingTypeStats),
    stats(this) {
  // TODO: apply civilization to game data
}
