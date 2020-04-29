// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QRect>
#include <QSize>

#include "FreeAge/common/civilizations.hpp"
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
  inline void SetToIntegerPopulationCount(int count) { doubledCount = -count * 2; }
  inline void SetToOneHalfPopulationCount() { doubledCount = -1; }

  inline int GetPopulationSpace() const { return doubledCount > 0 ? doubledCount / 2 : 0; }
  inline int GetDoubledPopulationSpace() const { return doubledCount > 0 ? doubledCount : 0; }
  inline int GetDoubledPopulationCount() const { return doubledCount < 0 ? -doubledCount : 0; }

 //private:

  /// The doubled population space if positive or demand if negative.
  int doubledCount;
};

struct ObjectTypeStats {

  /// TODO: not used by the game yet
  AttackType attackType;

  /// The seconds between two attacks.
  /// NOTE: The fire rate is the reciprocal of the attacks per second (the expected unit of fire rate?) to
  ///       keep the same with the original game. This also causes confusion on what "attack 10% faster" means.
  /// TODO: special case for technology upgrades #modifications
  /// TODO: not used by the game yet
  float fireRate;

  Damage damage;

  /// The min range of a non melee attack.
  /// TODO: not used by the game yet
  float minRange;

  /// The max range of a non melee attack.
  /// TODO: not used by the game yet
  float maxRange;

  // Attack accuracy.
  /// TODO: not used by the game yet
  float accuracy;

  /// NOTE: Melee or Ranged is considered a sub type of each AttackType and can be calculated from projectileSpeed

  /// The projectile speed or 0 if the attack is melee.
  /// TODO: not used by the game yet
  float projectileSpeed;

  inline bool IsAttackMelee() const { return projectileSpeed == 0; }
  inline bool IsAttackRanged() const { return projectileSpeed != 0; }

  /// The area of effect of the attack or 0 if there is none.
  /// TODO: not used by the game yet
  float areaOfEffectSize;

  /// The number of attacks performed. Special handling based on the attackType: 
  /// - MultipleProjectiles: the number of projectiles
  /// TODO: rename ?
  /// TODO: not used by the game yet
  int attacksCount;

  /// Duration from start of attack until damage being applied to target represended by the ratio
  /// of the full attack duration. To retrive the duration in seconds multiply by the attack duration.
  /// TODO: rename ?
  /// TODO: not used fully by the game yet (ability to cancel attack before the attackDelay mark)
  float attackDelay;

  /// If the attack can cause damage to friendly objects.
  /// TODO: not used by the game yet
  bool friendlyDamage;

  /// The max HP or 0 if invulnerable.
  int maxHp;

  /// Regenerated HP per minute (can be affected by technologies)
  /// TODO: not used by the game yet
  int regeneration;
  
  Armor armor;

  /// The line of sight radius.
  float lineOfSight;

  /// The number of seconds needed for its creation.
  float creationTime;
  /// The resources needed for its creation.
  ResourceAmount cost;

  /// TODO: not used by the game yet
  GarrisonType garrisonType;
  /// TODO: not used by the game yet
  int garrisonCapacity;

  /// Based on the type:
  /// - Villager, FishingShip: gather rate
  /// - Production building: production speed
  /// - Other: ?
  /// TODO: not used fully by the game yet
  float workRate;

  /// TODO: doc #monks
  int conversionResistanceLevel;

  /// Either the population space demand (population count) or the population space provided.
  PopulationCount population;

  /// The resources that can extracted a Villager. Used only by resource spot and animals
  /// TODO: not used by the game yet
  ResourceAmount resources;

};

struct UnitTypeStats : public ObjectTypeStats {

  /// The radius of the unit.
  /// TODO: clarify units
  float radius;

  /// The movement speed of the unit.
  /// TODO: clarify units
  float speed;
};

struct BuildingTypeStats : public ObjectTypeStats {

  /// Whether it acts as a drop-off point for each resource.
  bool dropOffPoint[static_cast<int>(ResourceType::NumTypes)];

  /// The size of the building in grid tile units.
  QSize size;

  /// The area of the building that is not traversable by units.
  /// TODO: Special handle for the gates #gates
  QRect occupancy;

  inline bool IsDropOffPointFor(ResourceType resourceType) const { return dropOffPoint[static_cast<int>(resourceType)]; }
};
