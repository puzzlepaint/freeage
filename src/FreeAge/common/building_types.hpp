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

QString GetBuildingName(BuildingType type);

/// Returns the max number of the given building type that the player can build.
/// Returns -1 if unlimited.
/// TODO: remove and have a check for only the Town Center bases on the civ stats #civs
int GetBuildingMaxInstances(BuildingType type);

int GetMaxElevationDifferenceForBuilding(BuildingType type);
