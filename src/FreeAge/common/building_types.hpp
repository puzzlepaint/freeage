// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QRect>
#include <QSize>
#include <QString>

#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/resources.hpp"

/// Building types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector of Sprite.
enum class BuildingType {
  // Player buildings
  TownCenter = 0,
  TownCenterBack,    // Not used as building, just for loading the sprite
  TownCenterCenter,  // Not used as building, just for loading the sprite
  TownCenterFront,   // Not used as building, just for loading the sprite
  TownCenterMain,    // Not used as building, just for loading the sprite
  
  House,
  Mill,
  MiningCamp,
  LumberCamp,
  Dock,
  
  Barracks,
  Outpost,
  PalisadeWall,
  PalisadeGate,
  
  // Gaia "buildings"
  TreeOak,
  FirstTree = TreeOak,
  LastTree = TreeOak,
  
  ForageBush,
  GoldMine,
  StoneMine,
  
  NumBuildings
};

inline bool IsTree(BuildingType type) {
  return type >= BuildingType::FirstTree &&
         type <= BuildingType::LastTree;
}

QSize GetBuildingSize(BuildingType type);
QRect GetBuildingOccupancy(BuildingType type);
QString GetBuildingName(BuildingType type);
double GetBuildingConstructionTime(BuildingType type);

/// TODO: This needs to consider the player's civilization and researched technologies
ResourceAmount GetBuildingCost(BuildingType type);

/// Returns whether the given building type acts as a drop-off point for the given resource type.
bool IsDropOffPointForResource(BuildingType building, ResourceType resource);

/// TODO: This needs to consider the player's civilization and researched technologies
u32 GetBuildingMaxHP(BuildingType type);
Armor GetBuildingArmor(BuildingType type);

/// Returns the max number of the given building type that the player can build.
/// Returns -1 if unlimited.
/// TODO: This needs to consider the player's age.
/// TODO: use infinity instead of -1 ?
int GetBuildingMaxInstances(BuildingType type);

int GetBuildingProvidedPopulationSpace(BuildingType type);

float GetBuildingLineOfSight(BuildingType type);

int GetMaxElevationDifferenceForBuilding(BuildingType type);
