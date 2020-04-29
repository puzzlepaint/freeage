// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/resources.hpp"

class TargetedModification;

// NOTE: There are scenarios where only the ages values are needed, so they must at the start
//       of the enum declaration.

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
  GreekFire,

  FirstUnique = GreekFire,
  LastUnique = GreekFire,

  NumTechnologies,
  NumAges = LastAge + 1,
};

inline bool IsAge(Technology tech) {
  return tech >= Technology::FirstAge &&
         tech <= Technology::LastAge;
}

inline bool IsUniqueTechnology(Technology tech) {
  return tech >= Technology::FirstAge &&
         tech <= Technology::LastAge;
}

// TODO: implement GetTechnologyName()

enum class TechnologyAvailability {
  /// Available after all requirements are reached.
  /// NOTE: requirements
  /// TODO: not used by the game yet
  Normal = 0,
  /// Unavailable for research.
  Unavailable,
  /// Will be applied with the start of the game.
  FreeFromStart,
  /// Will be applied as soon the age required is reached.
  /// TODO: not used by the game yet
  FreeFromRequiredAge,
  // Have been researched and cannot be researched again.
  Researched
};

struct TechnologyStats {

  /// The seconds needed to be researched.
  float researchDuration;

  /// The resources needed for researching.
  ResourceAmount cost;

  /// TODO: revisit when the dependecy "graph" for units/buildings/technologies is implemented #dependencies
  TechnologyAvailability availability;

  /// A collection of modifications that will be applied when the research is complete
  std::vector<TargetedModification> modifications;

};
