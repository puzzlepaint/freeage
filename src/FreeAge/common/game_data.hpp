// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/civilizations.hpp"
#include "FreeAge/common/type_stats.hpp"
#include "FreeAge/common/technologies.hpp"

// TODO: rename?
struct GameData {

  std::vector<UnitTypeStats> unitTypeStats;
  std::vector<BuildingTypeStats> buildingTypeStats;
  std::vector<TechnologyStats> technologyStats;
  std::vector<CivilizationStats> civilizationStats;
};
