// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QRect>
#include <QSize>

#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/resources.hpp"

static constexpr u32 typeStatsVersion = 1;

enum class AttackType {
  NoAttack = 0,
  /// TODO: rename ?
  Default,
  /// Full damage to all affected in the area of effect.
  HomogeneousAreaOfEffect,
  /// Full damage to primary target and 0.5 to all others in the area of effect.
  TrampleHalf, // War Elephants, Battle Elephant
  /// Full damage to primary target and 5 unaffected by armor damage in the area of effect.
  TrampleFivePure, // Cataphracts, Druzhina
  /// First projectile does full damage and other projectiles deal 3 pierce damage and 0 melee damage.
  MultipleProjectiles, // Chu Ko Nu, Kipchak
  
  TownCenter, Castle, Tower, BombardTower
};

enum class GarrisonType {
  /// No unit can be garrison.
  NoGarrison = 0,
  /// All units.
  AllUnits, // Transport ships
  /// Villagers, monks and foot soldiers only.
  VillagersAndMonksAndFootSoldiers, // TownCenter and towers
  /// Villagers and foot soldiers only.
  VillagersFootSoldiers, // Rams and Siege Towers
  /// All units except siege units.
  NonSiege, // Castle
  /// Villagers only.
  Villagers, // Khmer houses
  /// Units from production buildings can be trained directly into garrison,
  /// but not garrison from outside.
  Production,
  /// Same as Production plus Relics.
  ProductionAndRelics
};

struct PopulationCount {
  /// TODO (puzzlepaint): Refactor ?
  
  inline PopulationCount()
    : doubledCount(0) {};
  
  inline void SetToIntegerPopulationSpace(int count) { doubledCount = count * 2; }
  inline void SetToIntegerPopulationDemand(int count) { doubledCount = -count * 2; }
  inline void SetToOneHalfPopulationDemand() { doubledCount = -1; }

  inline int GetDoubledPopulationSpace() const { return doubledCount > 0 ? doubledCount : 0; }
  inline int GetDoubledPopulationDemand() const { return doubledCount < 0 ? -doubledCount : 0; }

 //private:

  /// The doubled population space if positive or demand if negative.
  int doubledCount;
};

struct ObjectTypeStats {

  float fireRate;
  Damage damage;

  float minRange;
  float maxRange;

  // Attack accuracy.
  float accuracy;

  AttackType attackType;

  /// NOTE: Melee or Ranged is considered a sub type of each AttackType and can be calculated from projectileSpeed

  /// The projectile speed or 0 if the attack is melee.
  float projectileSpeed;

  /// The area of effect of the attack or 0 if there is none.
  float areaOfEffectSize;

  /// The number of attacks performed. Special handling based on the attackType: 
  /// - MultipleProjectiles: the number of projectiles
  /// TODO: rename ?
  int attacksCount;

  /// Duration from start of attack until damage being applied to target represended by the ratio of the full attack
  /// duration. To retrive the duration in seconds multiply by the attack duration.
  /// TODO: rename ?
  float attackDelay;

  bool friendlyDamage;

  /// Regenerated HP per minute (can be affected by technologies)
  int regeneration;

  /// The max HP or 0 if invulnerable.
  int maxHp;

  Armor armor;

  /// The line of sight radius.
  float lineOfSight;

  /// The number of seconds needed for its creation.
  float creationTime;
  /// The resources needed for its creation.
  ResourceAmount cost;

  GarrisonType garrisonType;
  int garrisonCapacity;

  /// Based on the type:
  /// - Villager, FishingShip: gather rate
  /// - Production building: production speed
  /// - Other: ?
  float workRate;

  /// TODO: doc
  int conversionResistanceLevel;

  PopulationCount population;

  // Used only by resource spot and animals
  ResourceAmount resources;
};

struct UnitTypeStats : public ObjectTypeStats {

  /// TODO: doc
  float radius;

  /// TODO: doc
  float speed;
};

struct BuildingTypeStats : public ObjectTypeStats {

  /// Whether it acts as a drop-off point for each resource.
  bool dropOffPoint[static_cast<int>(ResourceType::NumTypes)];

  /// TODO: doc
  QSize size;

  /// TODO: doc
  QRect occupancy;

  inline bool IsDropOffPointFor(ResourceType resourceType) const { return dropOffPoint[static_cast<int>(resourceType)]; }
};

// TODO: rename?
struct GameData {

  std::vector<UnitTypeStats> unitTypeStats;
  std::vector<BuildingTypeStats> buildingTypeStats;

};
