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
  // A villager constructs a building.
  Construct = 0,
  
  // A villager collects a resource.
  CollectBerries,
  CollectWood,
  CollectGold,
  CollectStone,
  
  // A villager drops off carried resources at a resource drop-off building.
  DropOffResource,
  
  // A unit attacks an enemy unit or building
  Attack,

  // TODO (maanoo): ??, implement in some cases in GetInteractionType
  Garrison,
  
  // No valid interaction.
  Invalid
};
