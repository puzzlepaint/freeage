// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/common/civilizations.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/technologies.hpp"
#include "FreeAge/common/type_stats.hpp"

void LoadUnitTypeStats(std::vector<UnitTypeStats>& unitTypeStats);
void LoadBuildingTypeStats(std::vector<BuildingTypeStats>& buildingTypeStats);
void LoadTechnologyStats(std::vector<TechnologyStats>& technologyStats);
void LoadCivilizationStats(std::vector<CivilizationStats>& civilizationStats);

void LoadGameData(GameData& gameData);
