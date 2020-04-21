// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/player.hpp"
#include "FreeAge/common/damage.hpp"
#include "FreeAge/common/resources.hpp"
#include "FreeAge/common/object_types.hpp"

// Naming conversions:
// *Stats: collection of values
// *Info: the base *Stats, a list of modications on it and the resulting *Stats

// Player has a CivilizationInfo, a vector<UnitTypeInfo>, a vector<BuildingTypeInfo> and a vector<TechnologyInfo>
// The game data provide the base UnitTypeStats, BuildingTypeStats, TechnologyStats and CivilizationStats

// Note: The *Stats object do NOT contain information about the dependencies and building's interfaces (unit production,
//       technology research). The consept of the *Stats and *Info is to have the needed information by the game logic
//       in a ready to use *Stats struct, but also be able to provide to the gui information about the how the values
//       from the staring base *Stats have been calculated (by keep track of all the modications).
//       Dependencies and building's interfaces are accessed by different parts of the game logic and also there is no
//       need to know anything about the previous state.

// General structure
//
// Player
//  \ CivilizationInfo
//  \ vector<UnitTypeInfo>
//  \ vector<BuildingTypeInfo>
//  \ vector<TechnologyInfo>
//
// XxInfo
// \ XxStats baseStats                        (provided by the game data)
// \ std::vector<Modification> modifications  (starts empty)
// \ XxStats stats                            (updates every time the modifications change)
//
// XxStats
// \ raw values
//
// Modification
// \ Type        (what to modify)   (TODO: rename to Target?)
// \ Operation   (how to modify it)
// \ int value                (with two ints everything is possible)
// \ int extra
//

struct UnitTypeInfo {

  UnitTypeInfo(UnitType type, UnitTypeStats baseStats)
      : type(type),
        baseStats(baseStats) {
    stats = baseStats;
  }

  UnitType type;
  UnitTypeStats baseStats;   // immutable
  UnitTypeStats stats;       // mutable

  std::vector<Modification> modifications;

  void AddModification(Modification mod) {

    bool samePriorityAsLast = modifications.back().priority = mod.priority;
    if (samePriorityAsLast) {
      // this opt is not so important, could be removed
      modifications.push_back(mod);
      mod.ApplyToUnit(&stats, &baseStats);

    } else {
      modifications.push_back(mod); // TODO: instert at the correct position based on the priority

      // re-eval from the start
      stats = baseStats;
      for (auto i : modifications) {
        i.ApplyToUnit(&stats, &baseStats);
      }
    }
  }

  void MakeCurrentStastTheBaseStats() { // for converting (in order to hide the info about the opponent upgrades)
    modifications.clear();
    baseStats = stats;
  }

  // Gamelogic looks only at the stats.
  // Gui looks in baseStats.X for base values and
  // stat.X - baseStats.X for the bonus from technologies and
  // for more complex info at the modification list.
};

/// BuidlingTypeInfo, CivilizationInfo, TechnologyInfo
/// - a template thing
/// - copy paste

/// Note: The type, stats member are stored without a pointer and contain no pointers,
///       so there is no indirection in accessing every field. To minimize indirection
///       the two class should be stored in different vectors again without a pointer
///       eg. vector<UnitTypeInfo> unitInfos;


struct ObjectTypeStats {

  float fireRate;
  Damage damage;

  float minRange;
  float maxRange;

  float accuracy; // TODO: how many types of accuracy are there ?
  bool projectileSpeed;

  int maxHp;
  Armor armor;
  float lineOfSight;

  float creationTime;
  ResourceAmount cost;

  enum class GarrisonType {
    None,
    Units, // TransportShip ?
    VillagersAndFootArchersAndInfantry, // TownCenter and towers
    NonSiege, // Castle
    Villagers,  // Khmer houses
    /// Units from production buildings can be trained directly into garrison,
    /// but not garrison from outside.
    Production,
    /// Same as Production plus Relics can garrison
    ProductionAndRelics
  };

  GarrisonType garrisonType;
  int garrisonCapacity;

  /// Based on the type:
  /// - Villager, FishingShip: gather rate
  /// - Production building: production speed
  /// - Other: ?
  float workRate;

  int conversionResistanceLevel;

  // Used only by resource spot and animals
  ResourceAmount resources;
};

constexpr int sizeof_ObjectTypeStats = sizeof(ObjectTypeStats); 
// 320 = 2 * sizeof(Damage) + 80
// 200 types * 2 per info * 320 = 125K

struct UnitTypeStats : public ObjectTypeStats {

  float radius;
  float speed;
};

struct BuildingTypeStats : public ObjectTypeStats {

  int populationSpace;
  bool dropOffPoint[4];

  QSize size;
  QRect occupancy;

};


/// CivilizationStats contain every info that applies in a global level to all the units and
/// buildings.
///
/// CRITICAL: Also here are all the information about values that are needed for a single unit.
///
///           Example: villagerCarryingCapacity: an obvius design would be to add this
///           to the villager unit type that will contain this. Because only the Villager need
///           this, it would introduce some variation in the UnitTypeStats, like inheritance or
///           a list of generic attribute objects. The other solution is to put in the CivilizationStats.
///
///           Problems solved by keeping the UnitTypeStats as simple struct without pointers and lists.
///           - Can be placed inside a vector<UnitTypeStats> (without pointer) = memory management done
///           - The number of UnitTypeStats is known at start + a properly sized vector =
///             simple pointers to UnitTypeStats can be used safely
///           - Huge reduction of the code base.
///           - Cleaner implemention of the gamelogic.
///           - Simpler information retrieval for the gui.
///           - Performance (size, locality, less indirection)
///
///
struct CivilizationStats {

  // Add to the starting resource of the player
  ResourceAmount startingResourceD;

  UnitType startingScoutUnit;
  int startingVillagerCount;
  bool startingLlama;

  int populationCap;
  int bonusPopulationSpace;

  float garrisonHealRate;

  // TODO: monk conversion things

  float tributeFee;
  float tradingFree;
  float tradeGoldGeneration;
  float relicGoldGeneration;

  // == single unit values

  int farmFood;

  // NOTE: could aslo be stored as the workRate of an unspecialized villager
  float villagerConstuctionSpeed;

  float villagerCarryingCapacity[/* TODO: number of villager types*/];
  float fishingShipCarryingCapacity;

  bool singleTowerCenter;
  // use singleTowerCenter this instead of a max limit for every building (only the town center has
  // max limit, even the wonder can be build multiple types)

};

/// Represents a modification to something
struct Modification {

  static constexpr i32 Unset = std::numeric_limits<i32>::min();

  enum class Type {
    // unit, building and technology
    Cost,            // use extra to define ResourceType or leave Unset for to all of them

    // unit and building
    MaxHp,
    Damage,          // use extra to define DamageType
    Armor,           // use extra to define DamageType
    MinRange,
    MaxRange,
    FireRate,
    Accuracy,        // use extra to define type of accuracy ?
    LineOfSight,
    GarrisonCapacity,
    Upgrade,         // option 1: use value to define the old type, use extra to define the new unit (applied to civ)
                     // option 2: use value to define the new unit (applied to unit)

    // unit only
    Speed,
    ProductionTime,

    // building only
    ConstructionSpeed,
    WorkRate,
    PopulationSpace,

    // technology only
    ResearchTime,

    // civilization only
    VillagerCarryingCapacity,  // use extra to define villager type or leave Unset for all of them
    PopulationCap
  };

  // TODO: combine ProductionTime, ConstructionSpeed and ResearchTime ?

  // NOTE: Not all civilization stats need to have a corresponding modification type.
  //       Eg. the startingScoutUnit can just be set at the begging of the game and cannot be changed later.

  // TODO: find a clever way to compress all the monk conversion things

  enum class Operation {
    Set, // Set the value
    Add, // Add the value
    Mult, // Multiply the value
    MultAdd, // Multiply the base value and add the result to the value
  };

  int priority = 10;

  Type type;
  Operation operation;
  int value; // divide with 100.f in case of Mult

  int extra = Unset;

  // TODO: use a unified interface for TechnologyType, AgeAdvance and CivBonuses
  TechnologyType* source; // Optional

  // The base stats are passed for the Mult operation

  // Really nice old school macros
  #define CalculateCaseI(type, field) case type: stats->field = CalculateInt(stats->field, statsBase->field); break;
  #define CalculateCaseF(type, field) case type: stats->field = CalculateFloat(stats->field, statsBase->field); break;

  void ApplyToUnit(UnitTypeStats* stats, const UnitTypeStats* statsBase) {
    ApplyToObjectType(stats, statsBase);

    switch(type) {
      CalculateCaseF(Type::Speed, speed)
      CalculateCaseF(Type::ProductionTime, creationTime)
    }
  }
  void ApplyToBuilding(BuildingTypeStats* stats, const BuildingTypeStats* statsBase) const {
    ApplyToObjectType(stats, statsBase);

    switch(type) {
      // TODO: implement
    }
  }
  void ApplyToCivilization(CivilizationStats* stats, const CivilizationStats* statsBase) const {

    switch(type) {
      // TODO: implement
    }
  }

  // TODO: add the ApplyToTechnology

private:

  void ApplyToObjectType(ObjectTypeStats* stats, const ObjectTypeStats* statsBase) const {
    // TODO: Change the given stats

    switch(type) {

      CalculateCaseI(Type::MaxHp, maxHp)
      CalculateCaseF(Type::MaxRange, maxRange)
      CalculateCaseF(Type::FireRate, fireRate)

      case Type::Cost: {
        for (int i=0; i<4; i++) {
          if (extra == Unset || extra == i) {
            // TODO: extend the ResourceAmounts interface
            // stats->cost.Set(i, CalculateInt(stats->cost.Get(i), statsBase->cost.Get(i));
          }
        }
        break;
      }
      // TODO: implement all
    }
  }

  int CalculateInt(int current, int base) const {
    switch(operation) {
    case Operation::Set: return value;
    case Operation::Add: return current + value;
    case Operation::Mult: return current * 100.0f / value;
    case Operation::MultAdd: return current + base * 100.0f / value;
    }
  }
  float CalculateFloat(float current, float base) const {
    switch(operation) {
    case Operation::Set: return value;
    case Operation::Add: return current + value;
    case Operation::Mult: return current * 100.0f / value;
    case Operation::MultAdd: return current + base * 100.0f / value;
    }
  }

};


struct ObjectTypeFilter {

  enum class Type {
    AllUnits,
    UnitByType,
    UnitByArmor,
    AllBuildings,
    BuildingByType,
    BuildingByArmor,
    Technology,
    Civilization
  };

  Type type;
  int data; // UnitType, BuildingType or DamageType (use union ?)

  bool Matches(/* TODO: implement base on the use case */) const;
};

ObjectTypeFilter FilterOfUnitsByType(UnitType type);
ObjectTypeFilter FilterOfBuildingsByType(BuildingType type);
ObjectTypeFilter FilterOfUnitsByArmor(DamageType type);
ObjectTypeFilter FilterOfBuildingsByArmor(DamageType type);
ObjectTypeFilter FilterOfTechnology(TechnologyType type);


struct FilteredModification {
  ObjectTypeFilter filter;
  Modification modication;
};

enum class TechnologyType {
  Loom,
  // ...
  DarkAge
  // ...
  // TODO: fill
};

struct TechnologyStats {

  // TODO: implement a copy constructor to deep copy the modications vector,
  //       should behave like the modications is a static array in order to
  //       behave the same like the other Stats.

  ResourceAmount cost;
  float researchTime;

  std::vector<FilteredModification> modications;

  void addModification(FilteredModification x);

};

typedef TechnologyStats AgeStats;


// ===================================================================================
// === Usage examples
// ===================================================================================

// Technology
void test1() {

  TechnologyStats* loom = civilization.AddTechology(TechnologyType::Loom); // alocated inside a vector, return pointer
  loom->cost = ResourceAmount(0, 0, 50, 0);
  loom->researchTime = 25;
  // Villagers +15 HP, +1/+2 armor
  loom->addModification(
    FilterOfUnitsByArmor(DamageType::Villager), // easier that filter by type
    Modification(Type::MaxHp, Operation::Add, 15)
  );
  loom->addModification(
    FilterOfUnitsByArmor(DamageType::Villager),
    Modification(Type::Armor, Operation::Add, 1, DamageType::Melee)
  );
  loom->addModification(
    FilterOfUnitsByArmor(DamageType::Villager),
    Modification(Type::Armor, Operation::Add, 2, DamageType::Pierce)
  );

}

// Civ bonus examples
void test2() {

  typedef Modification::Type Type;
  typedef Modification::Operation Operation;

  // Town Center supports ten population (instead of five).
  age[0].addModification(
    FilterOfBuildingsByType(BuildingType::TownCenter),
    Modification(Type::PopulationSpace, Operation::Set, 10) // or Add 5
  );

  // Cavalry have +20% HP.
  age[0].addModification(
    FilterOfUnitsByArmor(DamageType::Cavalry),
    Modification(Type::MaxHp, Operation::MultAdd, 20)
  );

  // Town Centers cost -50% wood starting in the Castle Age.
  age[0].addModification(
    FilterOfBuildingsByType(BuildingType::TownCenter),
    Modification(Type::Cost, Operation::MultAdd, -50, static_cast<int>(ResourceType::Wood))
  );

  // Town Watch is free.
  age[0].addModification(
    FilterOfTechnology(TechnologyType::TownWatch),
    Modification(Type::Cost, Operation::Set, 0)
  }

  // Blacksmith upgrades cost no gold.
  age[0].addModification(
    FilterOfTechnologiesByResearchLocation(BuildingType::TownCenter),
    // TODO: can FilterOfTechnologiesByResearchLocation be impmented ?
    // This filter may be too complicated to implement, in that case use FilterOfTechnology
    // and repeat for every techonoly in the Blacksmith.
    Modification(Type::Cost, Operation::Set, 0, ResourceType::Gold)
  }
}


// Complete civ bonus example: Goths
{

  // Infantry are 20%/25%/30%/35% cheaper in the Dark/Feudal/Castle/Imperial Age.
  age[i].addModification(
    FilterOfUnitsByArmor(DamageType::Infantry),
    Modification(Type::Cost, Operation::MultAdd, {-20, -5, -5, -5}[i])
  );

  // Infantry have +1/+2/+3 attack bonus against standard buildings in the Feudal/Castle/Imperial Age.
  age[i].addModification(
    FilterOfUnitsByArmor(DamageType::Infantry),
    Modification(Type::Damage, Operation::Add, 1, DamageType::StandardBuilding)
  );

  // Villagers have +5 attack against Wild Boars and carry +15 food from hunting.
  age[0].addModification(
    FilterOfUnitsByArmor(DamageType::Villager), // easier that filter by type
    Modification(Type::Damage, Operation::Add, 5, DamageType::Boar)
  );
  age[0].addModification(
    ObjectTypeFilter::Civilization,
    Modification(Type::VillagerCarryingCapacity, Operation::Add, 15, UnitType::FemaleHunter)
  );  // repeat for UnitType::MaleHunter

  // +10 population cap in the Imperial Age.
  age[3].addModification(
    ObjectTypeFilter::Civilization,
    Modification(Type::PopulationCap, Operation::Add, 10)
  );

  // Team bonus: Barracks work 20% faster.
  age[3].teamBonus.addModification(
    FilterOfBuildingsByType(BuildingType::Barracks),
    Modification(Type::WorkRate, Operation::MultAdd, 20) // or use Mult with 1.2f
  )

}

// Unable to hanlde cases (yet):
// - Ethiopians: Receive +100 food and +100 gold whenever a new Age is reached.
// - Lithuanians: Each garrisoned Relic gives +1 attack to Knights and Leitis (maximum +4).
// - Tatars: Herdables contain +50% food. (how this is even work?)
//      (could be a boolean value at civ category)
// - Celts: Enemy herdables can be converted regardless of enemy units next to them.
// - Burmese: Relics are visible on the map from the game start.
// - Khmer: Khmer Villagers drop off food from farms (1 unit at a time) without needing a drop site
// - Vietnamese: Reveal enemy positions at game start.
//
// TODO: fill the list


// And how about Feitoria?

void Simulation() {
  // ...
  if (onOneSecondTick && player.stats.GetActiveBuildingTypeCount(BuildingType::Feitoria) > 0) { // a single array access
    player.resources.add(player.stats.GetActiveBuildingTypeCount(BuildingType::Feitoria) * FeitoriaGeneration);
  }
  // ...
}


// ===================================================================================
// === Unit Conversion
// ===================================================================================

// Here is a solution for the unit conversion problem under this design

struct Unit {

  Player* player;
  UnitType type;

  UnitTypeInfo *info;

  std::unordered_map<UnitType, UnitTypeInfo>* unitTypes = nullptr;
  // TODO: keep a copy of the CivilizationStats ? is it worth it ?

  // for not converted units the cost of this implemention
  // time: one pointer comperation per UnitType change (~0 per frame)

  Unit(UnitType type, ....., Player* sourcePlayer = nullptr) {

    if (sourcePlayer == nullptr) {
      info = player->GetUnitTypeInfo(type); // pointer valid until end of time
    } else {
      // this is a converted unit

      // GenerateStaticUnitTypeInfosNeededBy create a new map with copies of the
      // all the UnitTypeInfo that are needed by the given type
      allunittypes = sourcePlayer->GenerateStaticUnitTypeInfosNeededBy(type);
      // unitTypes should have for:
      // - Villager: all the villager types
      // - Trebucet: packed and unpacked
      // - rest: 1 element, the given type
      // TODO: is this list full ?

      info = &unitTypes->at(type);
      // info is allocated inside the allunittypes
    }
  }

  ~Unit() {
    if (IsConverted()) {
      delete allunittypes;
    }
  }

  bool IsConverted() {
    return player->GetUnitTypeInfo(type) != info;
  }

  void ChangeType(UnitType type) {
    // eg. lumberjack to hunter
    if (IsConverted) {
      info = &allunittypes->at(type);
      return;
    }
    ...
  }

  void UpgradeToNewType(UnitType type) {
    if (IsConverted) return false;
    ...
  }
}





