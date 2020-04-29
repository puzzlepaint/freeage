// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/game_data.hpp"
#include "FreeAge/common/unit_types.hpp"

class Player;
class ObjectTypeStats;
class TargetedModification;

/// A collection of stats for a player.
/// All of the stats can be reproduced from the game state, but they are kept track of here
/// for performace and ease of access to them.
///
/// Extension to the game logic should go at the private *Change methods:
///
///   void UnfinishedBuildingChange(BuildingType buildingType, int d)
///   void FinishedBuildingChange(BuildingType buildingType, int d)
///   void UnitChange(UnitType unitType, bool death, int d)
///   void Change()
///
struct PlayerStats {
 public:

  PlayerStats(const Player* player);
  ~PlayerStats();

  /// Called when new building is added.
  void BuildingAdded(BuildingType buildingType, bool finished);

  /// Called when new unit is added.
  void UnitAdded(UnitType unitType);

  /// Called when a building is destroyed.
  void BuildingRemoved(BuildingType buildingType, bool finished);

  /// Called when new unit is killed.
  void UnitRemoved(UnitType unitType);

  /// Called when a building changes type.
  void BuildingTransformed(BuildingType fromBuildingType, BuildingType toBuildingType);

  /// Called when a unit changes type.
  void UnitTransformed(UnitType fromUnitType, UnitType toUnitType);

  /// Called when a building construction is completeted.
  void BuildingFinished(BuildingType buildingType);

  /// TODO: implement, note that research can add population space.
  void ResearchCompleted(/* TODO */) {};

  /// The population of the units that are currently being produced.
  /// TODO: Implement on the client, now only used by the server. 
  /// TODO: Encapsulate and use the PopulationCounter
  int populationInProduction = 0;

  // getters

  inline int GetPopulationCount() const { return doubledPopulationCount / 2; }
  inline int GetPopulationSpace() const { return doubledPopulationSpace / 2; }

  /// The current population count of this player,
  /// *including units being produced*. This is required for "housed" checking,
  /// and it is different from the population count shown to the client.
  /// TODO: fix, for now the half values are ignored
  inline int GetPopulationCountIncludingInProduction() const { return GetPopulationCount() + populationInProduction; }

  /// The number of units with the given type that are alive.
  inline int GetUnitTypeCount(UnitType unitType) const {
    return unitsAlive[static_cast<int>(unitType)];
  }

  inline int GetVillagerCount() const { return villagerCount; }

  /// The number of buildings with the given type that have been constructred
  /// or are under construction and are alive.
  inline int GetBuildingTypeCount(BuildingType buildingType) const {
    int i = static_cast<int>(buildingType);
    return buildingConstructions[i] + buildingAlive[i];
  }
  
  /// Returns the number of existing, completed buildings with the given type.
  inline int GetBuildingTypeAlive(BuildingType buildingType) const {
    return buildingAlive[static_cast<int>(buildingType)];
  }

  /// If a building of the given type have ever been constructed.
  inline bool GetBuildingTypeExisted(BuildingType buildingType) const {
    return buildingExisted[static_cast<int>(buildingType)];
  }

  // debug

  void log() const;

 private:

  // core methods

  /// Called when the number of unfinished buildings change.
  void UnfinishedBuildingChange(BuildingType buildingType, int d);

  /// Called when the number of finished buildings change.
  void FinishedBuildingChange(BuildingType buildingType, int d);

  /// Called when the number of units change.
  void UnitChange(UnitType unitType, bool death, int d);

  /// Called when something active (i.e., excluding unfinished buildings) changes.
  void Change(const ObjectTypeStats& stats, int d);

  // TODO: change from arrays to maps if the sizeof(PlayerStats) gets too big ?
  // TODO: merge array of ints to a single array of structs ?

  /// The player that these stats refer to.
  const Player* player;

  /// The number of units alive per unit type.
  int unitsAlive[static_cast<int>(UnitType::NumUnits)];

  /// The number of all the units that are a type of villager (stored separately 
  /// because it's needed by the gui on every frame).
  int villagerCount = 0;

  /// The number of units died per unit type.
  int unitsDied[static_cast<int>(UnitType::NumUnits)];

  /// The current population space demand multiplied by 2 (to handle half values).
  int doubledPopulationCount = 0;

  /// The current population space supplied multiplied by 2 (to handle half values).
  int doubledPopulationSpace = 0;

  /// The number of buildings under construction per building type.
  int buildingConstructions[static_cast<int>(BuildingType::NumBuildings)];

  /// The number of built buildings per building type.
  int buildingAlive[static_cast<int>(BuildingType::NumBuildings)];

  /// If a building has ever been constructed per building type.
  bool buildingExisted[static_cast<int>(BuildingType::NumBuildings)];
};

class Player {
 public:

  /// TODO: rename int playerColorIndex to colorIndex
  Player(int index, int playerColorIndex, const GameData& gameData, Civilization civilization);

  void ApplyTechnologyModifications(Technology technology, const Player& basePlayer);
  void ApplyModification(const TargetedModification& targetedModification, const Player& basePlayer);

  inline const PlayerStats& GetPlayerStats() const { return stats; }
  inline PlayerStats& GetPlayerStats() { return stats; }

  inline const CivilizationStats& GetCivilizationStats() const { return civilizationStats; }
  inline const UnitTypeStats& GetUnitStats(UnitType unitType) const { return unitTypeStats.at(static_cast<int>(unitType)); }
  inline const BuildingTypeStats& GetBuildingStats(BuildingType buildingType) const { return buildingTypeStats.at(static_cast<int>(buildingType)); }
  inline const TechnologyStats& GetTechnologyStats(Technology technology) const { return technologyStats.at(static_cast<int>(technology)); }

  /// The unique player ID. The IDs are given to the players in consecutive order starting from 0
  /// and they are used as indexes to vectors and arrays containing items for each player. The gaia player
  /// has an index equal to kGaiaPlayerIndex which is a fixed value that does not follow the consecutive order.
  const int index;
  
  /// The player color index.
  /// TODO: should this be here?
  const int playerColorIndex;

  // TODO (maanoo): 
  const Civilization civilization;

  /// The current game resources of the player (wood, food, gold, stone).
  /// TODO: Allow modification to the resources only from a wrapper function
  ///       in order to have a single code location where player resources change?
  ResourceAmount resources;

 private:

  inline UnitTypeStats& GetModifiableUnitStats(UnitType unitType) { return unitTypeStats.at(static_cast<int>(unitType)); }
  inline BuildingTypeStats& GetModifiableBuildingStats(BuildingType buildingType) { return buildingTypeStats.at(static_cast<int>(buildingType)); }

  CivilizationStats civilizationStats;

  std::vector<UnitTypeStats> unitTypeStats;
  std::vector<BuildingTypeStats> buildingTypeStats;
  std::vector<TechnologyStats> technologyStats;

  PlayerStats stats;
};
