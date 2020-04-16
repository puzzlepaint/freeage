// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/unit_types.hpp"

struct PlayerStats {
 public:

  PlayerStats(); // TODO: pass the whatever the GetBuildingProvidedPopulationSpace will need
  ~PlayerStats();

  void BuildingAdded(BuildingType buildingType, bool finished);

  void UnitAdded(UnitType unitType);

  void BuildingRemoved(BuildingType buildingType, bool finished);

  void UnitRemoved(UnitType unitType);

  void BuildingTransformed(BuildingType fromBuildingType, BuildingType toBuildingType);

  void UnitTransformed(UnitType fromUnitType, UnitType toUnitType);

  void BuildingFinished(BuildingType buildingType);

  /// TODO: implement, note that research can add population space
  void ResearchCompleted(/* TODO */) {};

  // getters

  inline int GetPopulationCount() const { return populationCount; }
  inline int GetAvailablePopulationSpace() const { return availablePopulationSpace; }

  /// The number of buildings with the given type that have been constructred
  /// or are under construction.
  inline int GetBuildingTypeCount(BuildingType buildingType) const {
    int i = static_cast<int>(buildingType);
    return buildingConstructions[i] + buildingAlive[i];
  }

  /// If a building of the given type have ever been constructed.
  inline bool GetBuildingTypeExisted(BuildingType buildingType) const {
    return buildingExisted[static_cast<int>(buildingType)];
  }

private:

  // core methods

  void UnfinishedBuildingChange(BuildingType buildingType, int d);

  void FinishedBuildingChange(BuildingType buildingType, int d);

  void UnitChange(UnitType unitType, bool death, int d);

  // TODO: change from arrays to maps if the sizeof(PlayerStats) gets too big

  /// The number of units alive per unit type.
  int unitsAlive[static_cast<int>(UnitType::NumUnits)];

  /// The number of units died per unit type
  int unitsDied[static_cast<int>(UnitType::NumUnits)];

  /// The current population count.
  int populationCount = 0;

  /// The available population space.
  int availablePopulationSpace = 0;

  /// The number of buildings under construction per building type.
  int buildingConstructions[static_cast<int>(BuildingType::NumBuildings)];

  /// The number of built buildings per building type.
  int buildingAlive[static_cast<int>(BuildingType::NumBuildings)];

  /// If a building has ever been constructed per building type.
  bool buildingExisted[static_cast<int>(BuildingType::NumBuildings)];

};