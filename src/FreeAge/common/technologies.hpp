// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/resources.hpp"

enum class Technology {
  // Ages
  DarkAge = 0,
  FeudalAge,
  CastleAge,
  ImperialAge,

  FirstAge = DarkAge,
  LastAge = ImperialAge,

  // Dark age techologies
  Loom,

  // Unique technologies

  NumTechnologies
};

inline bool IsAge(Technology tech) {
  return tech >= Technology::FirstAge &&
         tech <= Technology::LastAge;
}

enum class TechnologyAvailability {
  /// Available after all requirements are reached.
  Normal = 0,
  /// Unavailable for research.
  Unavailable,
  /// Will be applied with the start of the game.
  FreeFromStart,
  /// Will be applied as soon the age required is reached.
  FreeFromRequiredAge
};

struct TechnologyStats {

  /// The seconds needed to be researched.
  float researchDuration;

  /// The resources needed for researching.
  ResourceAmount cost;

  TechnologyAvailability availability;

  // TODO (maanoo): add modification list

};