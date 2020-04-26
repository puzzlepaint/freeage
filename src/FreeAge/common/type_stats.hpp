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
  None,
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
  None,
  Units, // TransportShip ?
  VillagersAndFootArchersAndInfantry, // TownCenter and towers
  NonSiege, // Castle
  Villagers, // Khmer houses
  /// Units from production buildings can be trained directly into garrison,
  /// but not garrison from outside.
  Production,
  /// Same as Production plus Relics can garrison
  ProductionAndRelics
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

  // Used only by resource spot and animals
  ResourceAmount resources;
};

enum class PopulationDemand {
  None,
  One,
  Half
};

struct UnitTypeStats : public ObjectTypeStats {

  /// The population space needed.
  PopulationDemand populationDemand;

  /// TODO: doc
  float radius;

  /// TODO: doc
  float speed;
};

struct BuildingTypeStats : public ObjectTypeStats {

  /// TODO: doc
  int populationSpace;

  /// TODO: doc
  bool dropOffPoint[4];

  /// TODO: doc
  QSize size;

  /// TODO: doc
  QRect occupancy;
};
