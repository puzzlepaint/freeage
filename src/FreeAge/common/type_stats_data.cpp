// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/type_stats_data.hpp"

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/game_data.hpp"
#include "FreeAge/common/modifications.hpp"
#include "FreeAge/common/unit_types.hpp"

// UnitTypeStats helpers

inline void SetUnitDefaults(UnitTypeStats& s) {
  s.maxHp = -1;
  s.regeneration = 0;
  s.armor = GetUnitDefaultArmor();
  s.lineOfSight = 0;
  s.workRate = 1;
  s.garrisonType = GarrisonType::NoGarrison;
  s.garrisonCapacity = 0;
  s.conversionResistanceLevel = 0;
  s.resources = ResourceAmount();
  s.attackType = AttackType::Default;
  s.projectileSpeed = 0;
  s.areaOfEffectSize = 0;
  s.attacksCount = 1;
  s.fireRate = 0;
  s.minRange = 0;
  s.maxRange = 0;
  s.accuracy = 1;
  s.damage = GetUnitDefaultDamage();
  s.attackDelay = .5;
  s.friendlyDamage = false;
  s.population = PopulationCount();
  s.population.SetToIntegerPopulationCount(1);
  s.radius = 0;
  s.speed = 0;
}

inline void SetUnitBasic(UnitTypeStats& s, int maxHp, int meleeArmor, int pierceArmor, float radius, float speed, float lineOfSight) {
  s.maxHp = maxHp;
  s.armor.SetValue(DamageType::Melee, meleeArmor);
  s.armor.SetValue(DamageType::Pierce, pierceArmor);
  s.lineOfSight = lineOfSight;
  s.radius = radius;
  s.speed = speed;
}

inline void SetUnitArmor(UnitTypeStats& s, DamageType damageType, int value = 0) {
  s.armor.SetValue(damageType, value);
}

inline void SetUnitMeleeAttack(UnitTypeStats& s, float fireRate, int meleeDamage, float attackDelay) {
  s.attackType = AttackType::Default;
  s.projectileSpeed = 0;
  s.fireRate = fireRate;
  s.damage = GetUnitDefaultDamage();
  s.damage.SetValue(DamageType::Melee, meleeDamage);
  s.attackDelay = attackDelay;
}

inline void SetUnitRangeAttack(UnitTypeStats& s, float fireRate, int pierceDamage, float accuracy, float maxRange, float projectileSpeed, float attackDelay) {
  s.attackType = AttackType::Default;
  s.projectileSpeed = projectileSpeed;
  s.fireRate = fireRate;
  s.damage.SetValue(DamageType::Pierce, pierceDamage);
  s.accuracy = accuracy;
  s.maxRange = maxRange;
  s.projectileSpeed = projectileSpeed;
  s.attackDelay = attackDelay;
}

inline void SetUnitBonusDamage(UnitTypeStats& s, DamageType damageType, int value) {
  s.damage.SetValue(damageType, value);
}

inline void SetUnitCost(UnitTypeStats& s, float creationTime, u32 wood, u32 food, u32 gold, u32 stone) {
  s.creationTime = creationTime;
  s.cost = ResourceAmount(wood, food, gold, stone);
}

// BuildingTypeStats helpers

inline void SetBuildingDefaults(BuildingTypeStats& s) {
  s.maxHp = -1;
  s.regeneration = 0;
  s.armor = GetBuildingDefaultArmor();
  s.lineOfSight = 0;
  s.workRate = 1;
  s.garrisonType = GarrisonType::NoGarrison;
  s.garrisonCapacity = 0;
  s.conversionResistanceLevel = 3;
  s.resources = ResourceAmount();
  s.attackType = AttackType::NoAttack;
  s.projectileSpeed = 0;
  s.areaOfEffectSize = 0;
  s.attacksCount = 1;
  s.fireRate = 0;
  s.minRange = 0;
  s.maxRange = 0;
  s.accuracy = 1;
  s.damage = GetBuildingDefaultDamage();
  s.attackDelay = .5;
  s.friendlyDamage = false;
  s.population = PopulationCount();
  s.dropOffPoint[static_cast<int>(ResourceType::Wood)] = false;
  s.dropOffPoint[static_cast<int>(ResourceType::Food)] = false;
  s.dropOffPoint[static_cast<int>(ResourceType::Gold)] = false;
  s.dropOffPoint[static_cast<int>(ResourceType::Stone)] = false;
  s.size = QSize(0, 0);
  s.occupancy = QRect(0, 0, 0, 0);
}

inline void SetBuildingBasic(BuildingTypeStats& s, int maxHp, int meleeArmor, int pierceArmor, int size, float lineOfSight) {
  s.maxHp = maxHp;
  s.armor.SetValue(DamageType::Melee, meleeArmor);
  s.armor.SetValue(DamageType::Pierce, pierceArmor);
  s.armor.SetValue(DamageType::Building, 0);
  s.armor.SetValue(DamageType::StandardBuilding, 0);
  s.size = QSize(size, size);
  s.occupancy = QRect(0, 0, size, size);
  s.lineOfSight = lineOfSight;
}

inline void SetBuildingArmor(BuildingTypeStats& s, DamageType damageType, int value = 0) {
  s.armor.SetValue(damageType, value);
}

inline void SetBuildingGarrison(BuildingTypeStats& s, GarrisonType garrisonType, int garrisonCapacity) {
  s.garrisonType = garrisonType;
  s.garrisonCapacity = garrisonCapacity;
}

inline void SetBuildingAttack(BuildingTypeStats& s, AttackType attackType, float fireRate, int pierceDamage, float accuracy, float maxRange, float projectileSpeed, float attackDelay) {
  s.attackType = attackType;
  s.projectileSpeed = projectileSpeed;
  s.fireRate = fireRate;
  s.damage.SetValue(DamageType::Pierce, pierceDamage);
  s.accuracy = accuracy;
  s.maxRange = maxRange;
  s.projectileSpeed = projectileSpeed;
  s.attackDelay = attackDelay;
}

inline void SetBuildingBonusDamage(BuildingTypeStats& s, DamageType damageType, int value) {
  s.damage.SetValue(damageType, value);
}

inline void SetBuildingCost(BuildingTypeStats& s, float creationTime, u32 wood, u32 food, u32 gold, u32 stone) {
  s.creationTime = creationTime;
  s.cost = ResourceAmount(wood, food, gold, stone);
}

inline void SetTechnologyDefaults(TechnologyStats& s, Technology technology) {
  s.researchDuration = 0;
  s.cost = ResourceAmount();
  s.availability = IsAge(technology) || !IsUniqueTechnology(technology) ? TechnologyAvailability::Normal : TechnologyAvailability::Unavailable;
}

inline void SetTechnologyCost(TechnologyStats& s, float researchDuration, u32 wood, u32 food, u32 gold, u32 stone) {
  s.researchDuration = researchDuration;
  s.cost = ResourceAmount(wood, food, gold, stone);
}

inline void AddTechnologyModification(TechnologyStats& s, ObjectFilter filter, Modification modification) {
  s.modifications.emplace_back(filter, modification);
}

inline void SetCivilizationDefaults(CivilizationStats& s) {
  s.startingScoutUnit = UnitType::Scout;
  s.startingVillagerCount = 3;
  s.startingLlama = false;
  s.startingBonusResources = ResourceAmount();
  s.bonusMaxPopulation = 0;
  s.bonusPopulationSpace = 0;
  s.garrisonHealRate = 36;
  s.tradingFree = .3;
  s.tributeFee = .3;
  s.tradeGoldGeneration = 1;
  s.relicGoldGeneration = .5;
  s.maxTownCenters = 1;
  s.VillagerCarryingCapacity(UnitType::FemaleVillager) = 10;
  s.VillagerCarryingCapacity(UnitType::FemaleVillagerBuilder) = 10;
  s.VillagerCarryingCapacity(UnitType::FemaleVillagerForager) = 10;
  s.VillagerCarryingCapacity(UnitType::FemaleVillagerLumberjack) = 10;
  s.VillagerCarryingCapacity(UnitType::FemaleVillagerGoldMiner) =10;
  s.VillagerCarryingCapacity(UnitType::FemaleVillagerStoneMiner) = 10;
  s.fishingShipCarryingCapacity = 10; // TODO: find value ?
}

inline void AddCivilizationModification(CivilizationStats& s, Technology age, ObjectFilter filter, Modification modification) {
  s.modifications(age).emplace_back(filter, modification);
}

inline void AddCivilizationModification(CivilizationStats& s, int age, ObjectFilter filter, Modification modification) {
  s.modifications(static_cast<Technology>(age)).emplace_back(filter, modification);
}

inline void AddCivilizationTeamModification(CivilizationStats& s, ObjectFilter filter, Modification modification) {
  s.teamModifications.emplace_back(filter, modification);
}

// Vector helpers

inline UnitTypeStats& InitUnit(std::vector<UnitTypeStats>& unitTypeStats, UnitType type) {
  UnitTypeStats& s = unitTypeStats[static_cast<int>(type)];
  SetUnitDefaults(s);
  return s;
}
inline UnitTypeStats& CopyUnit(std::vector<UnitTypeStats>& unitTypeStats, UnitType type, UnitType source) {
  UnitTypeStats& s = unitTypeStats[static_cast<int>(type)];
  s = unitTypeStats[static_cast<int>(source)];
  return s;
}

inline BuildingTypeStats& InitBuilding(std::vector<BuildingTypeStats>& buildingTypeStats, BuildingType type) {
  BuildingTypeStats& s = buildingTypeStats[static_cast<int>(type)];
  SetBuildingDefaults(s);
  return s;
}
inline BuildingTypeStats& CopyBuilding(std::vector<BuildingTypeStats>& buildingTypeStats, BuildingType type, BuildingType source) {
  BuildingTypeStats& s = buildingTypeStats[static_cast<int>(type)];
  s = buildingTypeStats[static_cast<int>(source)];
  return s;
}

inline TechnologyStats& InitTechnology(std::vector<TechnologyStats>& technologyStats, Technology technology) {
  TechnologyStats& s = technologyStats[static_cast<int>(technology)];
  SetTechnologyDefaults(s, technology);
  return s;
}

inline CivilizationStats& InitCivilization(std::vector<CivilizationStats>& civilizationStats, Civilization type) {
  CivilizationStats& s = civilizationStats[static_cast<int>(type)];
  SetCivilizationDefaults(s);
  return s;
}

// Data

// NOTE: The following functions should be able to inline all the helper function calls and elimenate
//       any redundant value sets.

void LoadUnitTypeStats(std::vector<UnitTypeStats>& unitTypeStats) {
  // pre allocate all the UnitTypeStats and then fill the data
  unitTypeStats.resize(static_cast<int>(UnitType::NumUnits));

  { // Villager
    UnitTypeStats& s = InitUnit(unitTypeStats, UnitType::FemaleVillager);

    SetUnitBasic(s,
      /* hp     */ 25,
      /* armor  */ 0, 0,
      /* radius */ 0.15,
      /* speed  */ 0.8,
      /* los    */ 4);
    SetUnitArmor(s, DamageType::Villager);
    SetUnitMeleeAttack(s,
      /* fireRate    */ 2,
      /* meleeDamage */ 3,
      /* attackDelay */ .5); // TODO: attack delay?
    SetUnitBonusDamage(s, DamageType::StoneDefense, 6);
    SetUnitBonusDamage(s, DamageType::Building, 3);
    SetUnitBonusDamage(s, DamageType::Tree, 15); // assumption
    SetUnitCost(s, 25 /*seconds*/, 0 /*wood*/, 50 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }
  {
    UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerBuilder, UnitType::FemaleVillager);
    s.workRate = 1;
  }
  {
    UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerForager, UnitType::FemaleVillager);
    s.workRate = 0.31;
  }
  {
    UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerLumberjack, UnitType::FemaleVillager);
    s.workRate = 0.39;
  }
  {
    UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerGoldMiner, UnitType::FemaleVillager);
    s.workRate = 0.38;
  }
  {
    UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerStoneMiner, UnitType::FemaleVillager);
    s.workRate = 0.36;
  }
  // TODO: Add VillagerHunter
  // { // Villager Hunter
  //   UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::FemaleVillagerForager, UnitType::FemaleVillager); // TODO: replace *Forager with *Hunter, here for testing
  //
  //   s.workRate = 0.41;
  //   SetUnitRangeAttack(s,
  //     /* fireRate     */ 2,
  //     /* pierceDamage */ 4,
  //     /* accuracy     */ 1,   // assumption
  //     /* maxRange     */ 3,
  //     /* projectSpeed */ 6,   // assumption
  //     /* attackDelay  */ .3); // assumption // TODO: attack delay?
  // }

  // Duplicate
  CopyUnit(unitTypeStats, UnitType::MaleVillager, UnitType::FemaleVillager);
  CopyUnit(unitTypeStats, UnitType::MaleVillagerBuilder, UnitType::FemaleVillagerBuilder);
  CopyUnit(unitTypeStats, UnitType::MaleVillagerForager, UnitType::FemaleVillagerForager);
  CopyUnit(unitTypeStats, UnitType::MaleVillagerLumberjack, UnitType::FemaleVillagerLumberjack);
  CopyUnit(unitTypeStats, UnitType::MaleVillagerGoldMiner, UnitType::FemaleVillagerGoldMiner);
  CopyUnit(unitTypeStats, UnitType::MaleVillagerStoneMiner, UnitType::FemaleVillagerStoneMiner);

  { // Militia
    UnitTypeStats& s = InitUnit(unitTypeStats, UnitType::Militia);

    SetUnitBasic(s,
      /* hp     */ 40,
      /* armor  */ 0, 1,
      /* radius */ 0.15,
      /* speed  */ 0.9,
      /* los    */ 4);
    SetUnitArmor(s, DamageType::Infantry);
    SetUnitMeleeAttack(s,
      /* fireRate    */ 2,
      /* meleeDamage */ 4,
      /* attackDelay */ .5); // TODO: attack delay?
    SetUnitCost(s, 21 /*seconds*/, 0 /*wood*/, 60 /*food*/, 20 /*gold*/, 0 /*stone*/);
  }

  { // Scout Cavalry
    UnitTypeStats& s = InitUnit(unitTypeStats, UnitType::Scout);

    SetUnitBasic(s,
      /* hp     */ 45,
      /* armor  */ 0, 2,
      /* radius */ 0.3,
      /* speed  */ 1.2,
      /* los    */ 4);
    s.conversionResistanceLevel = 8;
    SetUnitArmor(s, DamageType::Cavalry);
    SetUnitMeleeAttack(s,
      /* fireRate    */ 2,
      /* meleeDamage */ 3,
      /* attackDelay */ .5); // TODO: attack delay?
    SetUnitBonusDamage(s, DamageType::Monk, 6);
    SetUnitCost(s, 30 /*seconds*/, 0 /*wood*/, 80 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  // TODO: remove, example of upgraded unit
  // { // Man-at-Arms
  //   UnitTypeStats& s = CopyUnit(unitTypeStats, UnitType::ManAtArms, UnitType::Militia);

  //   s.maxHp = 45;
  //   SetUnitMeleeAttack(s,
  //     /* fireRate    */ 2,
  //     /* meleeDamage */ 6,
  //     /* attackDelay */ .5); // TODO: attack delay?
  //   SetUnitBonusDamage(s, DamageType::EagleWarrior, 6);
  //   SetUnitBonusDamage(s, DamageType::StandardBuilding, 6);
  // }

}

void LoadBuildingTypeStats(std::vector<BuildingTypeStats>& buildingTypeStats) {
  // pre allocate all the BuildingTypeStats and then fill the data
  buildingTypeStats.resize(static_cast<int>(BuildingType::NumBuildings));

  { // Town Center
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::TownCenter);

    SetBuildingBasic(s,
      /* hp     */ 2400,
      /* armor  */ 3, 5,
      /* size   */ 4,
      /* los    */ 8);
    s.occupancy = QRect(0, 2, 2, 2);
    s.population.SetToIntegerPopulationSpace(5);
    SetBuildingGarrison(s, GarrisonType::VillagersAndMonksAndFootSoldiers, 15);
    s.dropOffPoint[static_cast<int>(ResourceType::Wood)] = true;
    s.dropOffPoint[static_cast<int>(ResourceType::Food)] = true;
    s.dropOffPoint[static_cast<int>(ResourceType::Gold)] = true;
    s.dropOffPoint[static_cast<int>(ResourceType::Stone)] = true;
    SetBuildingAttack(s,
      /* attack type  */ AttackType::TownCenter,
      /* fireRate     */ 2,
      /* pierceDamage */ 5,
      /* accuracy     */ 1,
      /* maxRange     */ 6,
      /* projectSpeed */ 7,
      /* attackDelay  */ .5); // TODO: attack delay?
    SetBuildingBonusDamage(s, DamageType::Ship, 5);
    SetBuildingBonusDamage(s, DamageType::Building, 5);
    SetBuildingBonusDamage(s, DamageType::Camel, 1);
    SetBuildingCost(s, 150 /*seconds*/, 275 /*wood*/, 0 /*food*/, 0 /*gold*/, 100 /*stone*/);
  }

  { // House
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::House);

    SetBuildingBasic(s,
      /* hp     */ 550,
      /* armor  */ 0, 7,
      /* size   */ 2,
      /* los    */ 2);
    s.population.SetToIntegerPopulationSpace(5);
    SetBuildingCost(s, 25 /*seconds*/, 25 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Mill
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::Mill);

    SetBuildingBasic(s,
      /* hp     */ 600,
      /* armor  */ 0, 7,
      /* size   */ 2,
      /* los    */ 6);
    s.dropOffPoint[static_cast<int>(ResourceType::Food)] = true;
    SetBuildingCost(s, 35 /*seconds*/, 100 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // MiningCamp
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::MiningCamp);

    SetBuildingBasic(s,
      /* hp     */ 600,
      /* armor  */ 0, 7,
      /* size   */ 2,
      /* los    */ 6);
    s.dropOffPoint[static_cast<int>(ResourceType::Gold)] = true;
    s.dropOffPoint[static_cast<int>(ResourceType::Stone)] = true;
    SetBuildingCost(s, 35 /*seconds*/, 100 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Lumber Camp
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::LumberCamp);

    SetBuildingBasic(s,
      /* hp     */ 600,
      /* armor  */ 0, 7,
      /* size   */ 2,
      /* los    */ 6);
    s.dropOffPoint[static_cast<int>(ResourceType::Wood)] = true;
    SetBuildingCost(s, 35 /*seconds*/, 100 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Dock
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::Dock);

    SetBuildingBasic(s,
      /* hp     */ 1800,
      /* armor  */ 0, 7,
      /* size   */ 3,
      /* los    */ 6);
    SetBuildingGarrison(s, GarrisonType::Production, 10);
    SetBuildingCost(s, 35 /*seconds*/, 150 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Barracks
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::Barracks);

    SetBuildingBasic(s,
      /* hp     */ 1200,
      /* armor  */ 0, 7,
      /* size   */ 3,
      /* los    */ 6);
    SetBuildingGarrison(s, GarrisonType::Production, 10);
    SetBuildingCost(s, 50 /*seconds*/, 175 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Outpost
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::Outpost);

    SetBuildingBasic(s,
      /* hp     */ 500,
      /* armor  */ 0, 0,
      /* size   */ 1,
      /* los    */ 6);
    SetBuildingCost(s, 15 /*seconds*/, 25 /*wood*/, 0 /*food*/, 0 /*gold*/, 5 /*stone*/);
  }

  { // Palisade Wall
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::PalisadeWall);

    SetBuildingBasic(s,
      /* hp     */ 250,
      /* armor  */ 2, 5,
      /* size   */ 1,
      /* los    */ 2);
    SetBuildingArmor(s, DamageType::WallAndGate);
    SetBuildingCost(s, 6 /*seconds*/, 2 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  { // Palisade Gate
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::PalisadeGate);

    // TODO: Palisade Gate armor should be 0/0 while under construction. Could be handled as a
    //       special case in the game logic.
    SetBuildingBasic(s,
      /* hp     */ 400,
      /* armor  */ 2, 2, // 0, 0 while under construction
      /* size   */ 1,
      /* los    */ 6);
    s.size = QSize(4, 1);
    s.occupancy = QRect(0, 0, 0, 0); // TODO: Occupancy of any Gate should be dynamic and handled by the game
    SetBuildingCost(s, 30 /*seconds*/, 30 /*wood*/, 0 /*food*/, 0 /*gold*/, 0 /*stone*/);
  }

  // Gaia "buildings"

  { // Tree Oak
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::TreeOak);

    SetBuildingBasic(s,
      /* hp     */ 20,
      /* armor  */ DamageValues::None, DamageValues::None, // assumption
      /* size   */ 1,
      /* los    */ 0);
    SetBuildingArmor(s, DamageType::Building, DamageValues::None);
    SetBuildingArmor(s, DamageType::StandardBuilding, DamageValues::None);
    SetBuildingArmor(s, DamageType::Tree);
    s.resources.wood() = 100; // Move the wood resource to the TreeStump
  }

  { // Forage Bush
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::ForageBush);

    SetBuildingBasic(s,
      /* hp     */ 0,
      /* armor  */ 0, 0,
      /* size   */ 1,
      /* los    */ 0);
    s.resources.food() = 125;
  }

  { // Gold Mine
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::GoldMine);

    SetBuildingBasic(s,
      /* hp     */ 0,
      /* armor  */ 0, 0,
      /* size   */ 1,
      /* los    */ 0);
    s.resources.gold() = 800;
  }
  { // Stone Mine
    BuildingTypeStats& s = InitBuilding(buildingTypeStats, BuildingType::StoneMine);

    SetBuildingBasic(s,
      /* hp     */ 0,
      /* armor  */ 0, 0,
      /* size   */ 1,
      /* los    */ 0);
    s.resources.stone() = 350;
  }
}

void LoadTechnologyStats(std::vector<TechnologyStats>& technologyStats) {
  // pre allocate all the TechnologyStats and then fill the data
  technologyStats.resize(static_cast<int>(Technology::NumTechnologies));

  using Type = ModificationType;
  using Operation = ModificationOperation;

  { // DarkAge
    InitTechnology(technologyStats, Technology::DarkAge);
  }

  auto SetAgeTechnologyCommonModifications = [] (TechnologyStats& s) {
    AddTechnologyModification(s,
        ObjectFilter::UnitByType(UnitType::Scout),
        Modification(Type::LineOfSight, Operation::Add, 2));
    AddTechnologyModification(s,
        ObjectFilter::BuildingsByType(BuildingType::Outpost),
        Modification(Type::LineOfSight, Operation::Add, 2));
    AddTechnologyModification(s,
        ObjectFilter::AllBuildingsExceptDefences(),
        Modification(ModificationType::Armor, Operation::Add, 1, DamageType::Melee));
    AddTechnologyModification(s,
        ObjectFilter::AllBuildingsExceptDefences(),
        Modification(ModificationType::Armor, Operation::Add, 1, DamageType::Pierce));
  };

  // TODO: With every age most buldings get more max hp depending on their type. Should this
  //       be stored in the modification list of the age or in the building?

  { // FeudalAge
    TechnologyStats& s = InitTechnology(technologyStats, Technology::FeudalAge);

    SetTechnologyCost(s, 130 /*seconds*/, 0 /*wood*/, 500 /*food*/, 0 /*gold*/, 0 /*stone*/);
    SetAgeTechnologyCommonModifications(s);
    AddTechnologyModification(s,
        ObjectFilter::UnitByType(UnitType::Scout),
        Modification(Type::Damage, Operation::Add, 2, DamageType::Melee));
    AddTechnologyModification(s,
        ObjectFilter::UnitByType(UnitType::Scout),
        Modification(Type::Damage, Operation::Add, .35, DamageType::Melee));
    AddTechnologyModification(s,
        ObjectFilter::UnitByType(UnitType::Scout),
        Modification(Type::Speed, Operation::MultAdd, +30));
    // TODO: implement building hp change
  }
  { // CastleAge
    TechnologyStats& s = InitTechnology(technologyStats, Technology::CastleAge);

    SetTechnologyCost(s, 160 /*seconds*/, 0 /*wood*/, 800 /*food*/, 200 /*gold*/, 0 /*stone*/);
    SetAgeTechnologyCommonModifications(s);
    // TODO: implement
  }
  { // ImperialAge
    TechnologyStats& s = InitTechnology(technologyStats, Technology::ImperialAge);

    SetTechnologyCost(s, 190 /*seconds*/, 0 /*wood*/, 1000 /*food*/, 800 /*gold*/, 0 /*stone*/);
    SetAgeTechnologyCommonModifications(s);
    // TODO: implement
  }

  { // Loom
    TechnologyStats& s = InitTechnology(technologyStats, Technology::Loom);

    SetTechnologyCost(s, 25 /*seconds*/, 0 /*wood*/, 0 /*food*/, 50 /*gold*/, 0 /*stone*/);
    AddTechnologyModification(s,
        ObjectFilter::UnitsByArmor(DamageType::Villager),
        Modification(ModificationType::MaxHp, Operation::Add, 15));
    AddTechnologyModification(s,
        ObjectFilter::UnitsByArmor(DamageType::Villager),
        Modification(ModificationType::Armor, Operation::Add, 1, DamageType::Melee));
    AddTechnologyModification(s,
        ObjectFilter::UnitsByArmor(DamageType::Villager),
        Modification(ModificationType::Armor, Operation::Add, 2, DamageType::Pierce));
  }

  { // Greek Fire
    TechnologyStats& s = InitTechnology(technologyStats, Technology::GreekFire);

    SetTechnologyCost(s, 40 /*seconds*/, 0 /*wood*/, 250 /*food*/, 300 /*gold*/, 0 /*stone*/);
    AddTechnologyModification(s,
        ObjectFilter::UnitByType(UnitType::Scout), // TODO: change with FireShip
        Modification(ModificationType::MaxRange, Operation::Add, 1));
  }

}

void LoadCivilizationStats(std::vector<CivilizationStats>& civilizationStats) {
  // pre allocate all the BuildingTypeStats and then fill the data
  civilizationStats.resize(static_cast<int>(Civilization::NumCivilizations));

  using Type = ModificationType;
  using Operation = ModificationOperation;

  { // Gaia
    InitCivilization(civilizationStats, Civilization::Gaia);
  }
  { // ReplaceWithPuzzlepaintsFavoriteCivilization
    InitCivilization(civilizationStats, Civilization::ReplaceWithPuzzlepaintsFavoriteCivilization);
    // TODO: implement
  }
  { // Byzantines
    CivilizationStats& s = InitCivilization(civilizationStats, Civilization::Byzantines);
    
    AddCivilizationModification(s, 
        Technology::DarkAge,
        ObjectFilter::UnitByType(UnitType::Militia), // TODO: replace with Camel Riders, Skirmishers, and the Spearman
        Modification(Type::Cost, Operation::MultAdd, -25));
    AddCivilizationModification(s, 
        Technology::DarkAge,
        ObjectFilter::UnitByType(UnitType::Militia), // TODO: replace with FireShip
        Modification(Type::FireRate, Operation::MultAdd, 20));
    AddCivilizationModification(s, 
        Technology::DarkAge,
        ObjectFilter::TechnologyByType(Technology::ImperialAge),
        Modification(Type::Cost, Operation::MultAdd, -33));
    AddCivilizationModification(s, 
        Technology::DarkAge,
        ObjectFilter::TechnologyByType(Technology::Loom), // TODO: replace with Town Watch
        Modification(Type::TechnologyAvailability, Operation::Set, TechnologyAvailability::FreeFromRequiredAge));

    for (int age = 0; age < static_cast<int>(Technology::NumAges); ++ age) {
      AddCivilizationModification(s, 
          age,
          ObjectFilter::AllTechnologies(),
          Modification(Type::MaxHp, Operation::MultAdd, 10));
    }

    AddCivilizationTeamModification(s,
        ObjectFilter::Civilization(),
        Modification(Type::MonkHealRate, Operation::MultAdd, 50));
  }

}

void LoadGameData(GameData& gameData) {
  LoadUnitTypeStats(gameData.unitTypeStats);
  LoadBuildingTypeStats(gameData.buildingTypeStats);
  LoadTechnologyStats(gameData.technologyStats);
  LoadCivilizationStats(gameData.civilizationStats);

  // Changes to game data for development testing

  gameData.unitTypeStats[static_cast<int>(UnitType::MaleVillagerBuilder)].workRate = 3;
  gameData.unitTypeStats[static_cast<int>(UnitType::FemaleVillagerBuilder)].workRate = 3;
  gameData.unitTypeStats[static_cast<int>(UnitType::MaleVillager)].creationTime = 10;
  gameData.unitTypeStats[static_cast<int>(UnitType::Militia)].creationTime = 3;
  gameData.buildingTypeStats[static_cast<int>(BuildingType::Outpost)].lineOfSight = 300;

}