// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include <gtest/gtest.h>
#include <QApplication>

#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/player.hpp"
#include "FreeAge/common/type_stats_data.hpp"
#include "FreeAge/client/map.hpp"

int main(int argc, char** argv) {
  // Initialize loguru
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  // Initialize GoogleTest
  ::testing::InitGoogleTest(&argc, argv);
  
  // Initialize Qt
  QApplication qapp(argc, argv);
  
  // Run the tests
  return RUN_ALL_TESTS();
}

static std::shared_ptr<Player> CreateTestingPlayer() {
  // Load the game data once
  static GameData gameData;
  if (gameData.unitTypeStats.empty()) {
    LoadGameData(gameData);
  }
  // Create and return new players
  std::shared_ptr<Player> player(new Player(0, 0, gameData));
  return player;
}

static void TestProjectedCoordToMapCoord(const Map& map) {
  constexpr bool kDebug = false;
  constexpr int kNumTests = 10;
  for (int test = 0; test < kNumTests; ++ test) {
    QPointF mapCoord(
        map.GetWidth() * ((rand() % 100000) / static_cast<float>(100000 - 1)),
        map.GetHeight() * ((rand() % 100000) / static_cast<float>(100000 - 1)));
    if (kDebug) {
      LOG(INFO) << "-- Test " << test << " --";
      LOG(INFO) << "mapCoord: " << mapCoord.x() << ", " << mapCoord.y();
    }
    QPointF projectedCoord = map.MapCoordToProjectedCoord(mapCoord);
    if (kDebug) {
      LOG(INFO) << "projectedCoord: " << projectedCoord.x() << ", " << projectedCoord.y();
    }
    QPointF mapCoord2;
    bool ok = map.ProjectedCoordToMapCoord(projectedCoord, &mapCoord2);
    EXPECT_TRUE(ok);
    if (ok) {
      EXPECT_NEAR(mapCoord.x(), mapCoord2.x(), 1e-3f);
      EXPECT_NEAR(mapCoord.y(), mapCoord2.y(), 1e-3f);
    }
  }
}

TEST(Map, CoordinateConversion_FlatMap) {
  srand(0);
  
  constexpr int kMapWidth = 15;
  constexpr int kMapHeight = 15;
  Map testMap(kMapWidth, kMapHeight);
  
  TestProjectedCoordToMapCoord(testMap);
}

TEST(Map, CoordinateConversion_HillyMap) {
  srand(0);
  
  constexpr int kMapWidth = 15;
  constexpr int kMapHeight = 15;
  Map testMap(kMapWidth, kMapHeight);
  
  for (int y = 0; y < testMap.GetHeight(); ++ y) {
    for (int x = 0; x < testMap.GetWidth(); ++ x) {
      testMap.elevationAt(x, y) = rand() % 2;
    }
  }
  
  TestProjectedCoordToMapCoord(testMap);
}

TEST(PlayerStats, Operations) {
  std::shared_ptr<Player> player = CreateTestingPlayer();

  PlayerStats& stats = player->GetPlayerStats();
  LOG(INFO) << "sizeof(PlayerStats) = " << sizeof(PlayerStats);

  EXPECT_EQ(stats.GetBuildingTypeCount(BuildingType::Barracks), 0);
  EXPECT_FALSE(stats.GetBuildingTypeExisted(BuildingType::Barracks));

  stats.BuildingAdded(BuildingType::House, true);
  stats.BuildingAdded(BuildingType::House, false);
  stats.BuildingAdded(BuildingType::Barracks, true);
  stats.BuildingAdded(BuildingType::House, false);
  stats.BuildingFinished(BuildingType::House);
  stats.UnitAdded(UnitType::FemaleVillager);
  stats.UnitAdded(UnitType::MaleVillager);

  EXPECT_EQ(stats.GetPopulationSpace(), 10);
  EXPECT_EQ(stats.GetPopulationCount(), 2);
  EXPECT_EQ(stats.GetBuildingTypeCount(BuildingType::Barracks), 1);
  EXPECT_TRUE(stats.GetBuildingTypeExisted(BuildingType::Barracks));

  stats.BuildingRemoved(BuildingType::House, true);
  stats.BuildingRemoved(BuildingType::Barracks, true);
  stats.UnitTransformed(UnitType::FemaleVillager, UnitType::FemaleVillagerGoldMiner);
  stats.UnitRemoved(UnitType::MaleVillager);

  EXPECT_EQ(stats.GetPopulationSpace(), 5);
  EXPECT_EQ(stats.GetPopulationCount(), 1);
  EXPECT_EQ(stats.GetBuildingTypeCount(BuildingType::Barracks), 0);
  EXPECT_TRUE(stats.GetBuildingTypeExisted(BuildingType::Barracks));

}

TEST(Damage, DamageValues) {
  LOG(INFO) << "sizeof(DamageValues) = " << sizeof(DamageValues);

  Armor armor = GetUnitDefaultArmor();
  Damage damage = GetBuildingDefaultDamage();

  EXPECT_EQ(armor.melee(), 0);
  EXPECT_EQ(armor.pierce(), 0);
  EXPECT_EQ(damage.melee(), Damage::None);
  EXPECT_EQ(damage.GetValue(DamageType::Building), Damage::None);

  armor.AddValue(DamageType::Melee, 1);
  damage.AddValue(DamageType::Melee, 1);

  EXPECT_EQ(armor.GetValue(DamageType::Melee), 1);
  EXPECT_EQ(damage.GetValue(DamageType::Melee), 1);

}

TEST(Damage, CalculateDamage) {

  Damage archerDamage = GetUnitDefaultDamage();
  archerDamage.SetValue(DamageType::Pierce, 4);
  archerDamage.SetValue(DamageType::Spearman, 3);

  Armor villagerArmor = GetUnitDefaultArmor();

  Armor spearmanArmor = GetUnitDefaultArmor();
  spearmanArmor.SetValue(DamageType::Melee, 0);
  spearmanArmor.SetValue(DamageType::Pierce, 0);
  spearmanArmor.SetValue(DamageType::Infantry, 0);
  spearmanArmor.SetValue(DamageType::Spearman, 0);

  EXPECT_EQ(CalculateDamage(archerDamage, villagerArmor), 4);
  EXPECT_EQ(CalculateDamage(archerDamage, spearmanArmor), 7);

  villagerArmor.AddValue(DamageType::Pierce, 2);
  spearmanArmor.AddValue(DamageType::Pierce, 1);

  EXPECT_EQ(CalculateDamage(archerDamage, villagerArmor), 2);
  EXPECT_EQ(CalculateDamage(archerDamage, spearmanArmor), 6);

  Armor ramArmor = GetUnitDefaultArmor();
  ramArmor.SetValue(DamageType::Melee, -3);

  Damage villagerDamage = GetUnitDefaultDamage();
  villagerDamage.SetValue(DamageType::Melee, 3);

  EXPECT_EQ(CalculateDamage(villagerDamage, ramArmor), 6);

}

TEST(GameLogic, VillagerVsTree) {
  // Test the number of hits needed to chop a tree.
  std::shared_ptr<Player> player = CreateTestingPlayer();

  Damage villagerDamage = player->GetUnitStats(UnitType::MaleVillagerLumberjack).damage;
  Armor treeArmor = player->GetBuildingStats(BuildingType::TreeOak).armor;

  // kill in two hits
  EXPECT_EQ(CalculateDamage(villagerDamage, treeArmor), 15);

  // apply the Sappers technology
  villagerDamage.AddValue(DamageType::Building, 15);
  villagerDamage.AddValue(DamageType::StoneDefense, 15);
  villagerDamage.AddValue(DamageType::Tree, 5); // assumption

  // kill in one hit
  EXPECT_GE(CalculateDamage(villagerDamage, treeArmor), 20);

}

TEST(DataValidation, UnitTypeStats) {
  LOG(INFO) << "sizeof(UnitTypeStats) * UnitType::NumUnits = " 
    << sizeof(UnitTypeStats) << " * " << static_cast<int>(UnitType::NumUnits) << " = "
    << sizeof(UnitTypeStats) * static_cast<int>(UnitType::NumUnits);

  // NOTE: This could be e extracted and moved to the game in order to validate data loaded
  //       dynamicly from files.

  std::vector<UnitTypeStats> unitTypeStats;
  LoadUnitTypeStats(unitTypeStats);

  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    // TODO: get the internal name
    std::string name = GetUnitName(static_cast<UnitType>(unitType)).toStdString();
    UnitTypeStats& s = unitTypeStats.at(unitType);

    EXPECT_GE(s.maxHp, 0) << name;
    EXPECT_GT(s.radius, 0) << name;
    if (s.attackType != AttackType::NoAttack) {
      EXPECT_GT(s.fireRate, 0) << name;
      EXPECT_GE(s.maxRange, s.minRange) << name;
    }
  }
}

inline bool IsRealBuilding(BuildingType type) {
  return !(type >= BuildingType::TownCenterBack &&
           type <= BuildingType::TownCenterMain); 
}

TEST(DataValidation, BuildingTypeStats) {
  LOG(INFO) << "sizeof(BuildingTypeStats) * BuildingType::NumBuildings = " 
    << sizeof(BuildingTypeStats) << " * " << static_cast<int>(BuildingType::NumBuildings) << " = "
    << sizeof(BuildingTypeStats) * static_cast<int>(BuildingType::NumBuildings);
    
  // NOTE: This could be e extracted and moved to the game in order to validate data loaded 
  //       dynamicly from files. 

  std::vector<BuildingTypeStats> buildingTypeStats;
  LoadBuildingTypeStats(buildingTypeStats);

  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    if (!IsRealBuilding(static_cast<BuildingType>(buildingType))) {
      continue;
    }
    // TODO: get the internal name
    std::string name = GetBuildingName(static_cast<BuildingType>(buildingType)).toStdString();
    BuildingTypeStats& s = buildingTypeStats.at(buildingType);

    EXPECT_GE(s.maxHp, 0) << name;
    EXPECT_GT(s.size.height(), 0) << name;
    EXPECT_GT(s.size.width(), 0) << name;
    // TODO: proper check the occupancy is contained in the size
    EXPECT_LE(s.occupancy.height(), s.size.height()) << name;
    EXPECT_LE(s.occupancy.width(), s.size.width()) << name;
  }
}