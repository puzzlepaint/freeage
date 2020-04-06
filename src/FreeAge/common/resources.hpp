#pragma once

#include <QObject>
#include <QString>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"

enum class ResourceType {
  Wood = 0,
  Food = 1,
  Gold = 2,
  Stone = 3,
  NumTypes = 4
};

inline QString GetResourceName(ResourceType type) {
  if (type == ResourceType::Wood) {
    return QObject::tr("wood");
  } else if (type == ResourceType::Food) {
    return QObject::tr("food");
  } else if (type == ResourceType::Gold) {
    return QObject::tr("gold");
  } else if (type == ResourceType::Stone) {
    return QObject::tr("stone");
  }
  
  LOG(ERROR) << "GetResourceName() called on invalid resource type: " << static_cast<int>(type);
  return "";
}

struct ResourceAmount {
  inline ResourceAmount()
      : resources{0, 0, 0, 0} {}
  
  inline ResourceAmount(u32 wood, u32 food, u32 gold, u32 stone)
      : resources{wood, food, gold, stone} {}
  
  /// Returns true if with this resource amount, one can afford to buy something that
  /// costs the other resource amount. I.e., for every resource, this amount has at
  /// least as much as the other.
  inline bool CanAfford(const ResourceAmount& other) const {
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      if (other.resources[i] > resources[i]) {
        return false;
      }
    }
    return true;
  }
  
  inline void Add(const ResourceAmount& value) {
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      resources[i] += value.resources[i];
    }
  }
  
  inline void Subtract(const ResourceAmount& value) {
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      resources[i] -= value.resources[i];
    }
  }
  
  inline u32& wood() { return resources[static_cast<int>(ResourceType::Wood)]; }
  inline const u32& wood() const { return resources[static_cast<int>(ResourceType::Wood)]; }
  
  inline u32& food() { return resources[static_cast<int>(ResourceType::Food)]; }
  inline const u32& food() const { return resources[static_cast<int>(ResourceType::Food)]; }
  
  inline u32& gold() { return resources[static_cast<int>(ResourceType::Gold)]; }
  inline const u32& gold() const { return resources[static_cast<int>(ResourceType::Gold)]; }
  
  inline u32& stone() { return resources[static_cast<int>(ResourceType::Stone)]; }
  inline const u32& stone() const { return resources[static_cast<int>(ResourceType::Stone)]; }
  
  inline bool operator == (const ResourceAmount& other) const {
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      if (resources[i] != other.resources[i]) {
        return false;
      }
    }
    return true;
  }
  
  inline bool operator != (const ResourceAmount& other) const {
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      if (resources[i] != other.resources[i]) {
        return true;
      }
    }
    return false;
  }
  
  
  u32 resources[static_cast<int>(ResourceType::NumTypes)];
};

inline ResourceAmount operator * (float factor, const ResourceAmount& value) {
  return ResourceAmount(
      factor * value.wood() + 0.5f,
      factor * value.food() + 0.5f,
      factor * value.gold() + 0.5f,
      factor * value.stone() + 0.5f);
}
