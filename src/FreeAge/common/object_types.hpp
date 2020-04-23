// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <limits>

#include "FreeAge/common/free_age.hpp"

enum class ObjectType {
  Building = 0,
  Unit = 1
};

constexpr u32 kInvalidObjectId = std::numeric_limits<u32>::max();

enum class InteractionType {
  // TODO (maanoo): doc
  Unknown = 0,

  // A villager constructs a building.
  Construct,
  
  // A villager collects a resource.
  CollectBerries,
  CollectWood,
  CollectGold,
  CollectStone,
  
  // A villager drops off carried resources at a resource drop-off building.
  DropOffResource,
  
  // A unit attacks an enemy unit or building
  Attack,

  // A unit garrison in a other unit or building.
  Garrison,
  Ungarrison,
  
  // No valid interaction.
  Invalid
};
