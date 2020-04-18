// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/player.hpp"

#include <cassert>

PlayerStats::PlayerStats() {
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

  Change();
}

void PlayerStats::FinishedBuildingChange(BuildingType buildingType, int d) {
  buildingAlive[static_cast<int>(buildingType)] += d;
  if (d > 0) {
    buildingExisted[static_cast<int>(buildingType)] = true;
  }

  availablePopulationSpace += d * GetBuildingProvidedPopulationSpace(buildingType);

  // TODO: implement special case: Feitoria

  Change();
}

void PlayerStats::UnitChange(UnitType unitType, bool death, int d) {
  assert(!death || d < 0); // death => d < 0

  populationCount += d;

  unitsAlive[static_cast<int>(unitType)] += d;
  if (death) {
    unitsDied[static_cast<int>(unitType)] += -d;
  }

  Change();
}

void PlayerStats::Change() {
  // log();
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
