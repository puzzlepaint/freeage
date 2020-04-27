// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <vector>

#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/client/object.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/texture.hpp"

class Map;
class Match;


constexpr int kNumFacingDirections = 16;

enum class UnitAnimation {
  Idle = 0,
  CarryIdle,
  Walk,
  CarryWalk,
  Task,
  Attack,
  Death,
  CarryDeath,
  Decay,
  CarryDecay,
  NumAnimationTypes
};


/// Stores client-side data for unit types (i.e., their graphics).
/// Access the global unit types vector via GetUnitTypes().
class ClientUnitType {
 public:
  ClientUnitType() = default;
  ~ClientUnitType();
  
  bool Load(UnitType type, const std::filesystem::path& graphicsSubPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes);
  
  int GetHealthBarHeightAboveCenter() const;
  
  inline const std::vector<SpriteAndTextures*>& GetAnimations(UnitAnimation type) const { return animations[static_cast<int>(type)]; }
  
  inline const Texture* GetIconTexture() const { return iconTexture; }
  
  /// Returns the global instance of the unit types vector.
  inline static std::vector<ClientUnitType>& GetUnitTypes() {
    static std::vector<ClientUnitType> unitTypesSingleton;
    return unitTypesSingleton;
  }
  
 private:
  bool LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsSubPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes, UnitAnimation type);
  
  /// Indexed by: [static_cast<int>(UnitAnimation animation)][animation_variant]
  std::vector<std::vector<SpriteAndTextures*>> animations;
  
  /// The maximum centerY value of any graphic frame of this unit type in the idle animation(s) when facing right.
  /// This can be used to determine a reasonable height for the unit's health bar.
  int maxCenterY;
  
  Texture* iconTexture = nullptr;
};

/// Convenience function that returns the ClientUnitType for a given unit type.
inline ClientUnitType& GetClientUnitType(UnitType type) {
  return ClientUnitType::GetUnitTypes()[static_cast<int>(type)];
}


/// Represents a unit on the client side.
class ClientUnit : public ClientObject {
 public:
  ClientUnit(Player* player, UnitType type, const QPointF& mapCoord, u32 hp);
  
  /// Returns the projected coordinates of this unit's center point.
  QPointF GetCenterProjectedCoord(Map* map);
  
  /// Computes the sprite rectangle for this unit in projected coordinates.
  QRectF GetRectInProjectedCoords(
      Map* map,
      double serverTime,
      bool shadow,
      bool outline);
  
  void Render(
      Map* map,
      QRgb outlineOrModulationColor,
      SpriteShader* spriteShader,
      float* viewMatrix,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double serverTime,
      bool shadow,
      bool outline);
  
  inline UnitType GetType() const { return type; }
  inline const UnitTypeStats& GetStats() const { return GetPlayer()->GetUnitStats(type); }
  virtual const ObjectTypeStats& GetObjectStats() const { return GetStats(); }

  inline void SetType(UnitType newType) { type = newType; }
  
  /// Convenience function that returns the ClientUnitType for this unit.
  inline ClientUnitType& GetClientUnitType() const {
    return ClientUnitType::GetUnitTypes()[static_cast<int>(type)];
  }
  
  inline QString GetUnitName() const { return ::GetUnitName(type); }
  inline const Texture* GetIconTexture() const { return ClientUnitType::GetUnitTypes()[static_cast<int>(type)].GetIconTexture(); };
  
  inline UnitAnimation GetCurrentAnimation() const { return currentAnimation; }
  void SetCurrentAnimation(UnitAnimation animation, double serverTime);
  
  Texture& GetTexture(bool shadow);
  
  inline const QPointF& GetMapCoord() const {
    if (IsGarrisoned()) {
      LOG(ERROR) << "GetMapCoord() must not be called on garrisoned units.";
    }
    return mapCoord;
  }
  inline int GetDirection() const { return direction; }
  
  void SetMovementSegment(double serverTime, const QPointF& startPoint, const QPointF& speed, UnitAction action, Map* map, Match* match);
  
  // Must be called after teleport like position changes.
  inline void ClearOverrideDirection() { overrideDirectionExpireTime = -1; }

  inline bool IsGarrisoned() const { return garrisonedObject != nullptr; }

  inline void SetCarriedResources(ResourceType type, u8 amount) {
    carriedResourceType = type;
    carriedResourceAmount = amount;
  }
  inline ResourceType GetCarriedResourceType() const { return carriedResourceType; }
  inline int GetCarriedResourceAmount() const { return carriedResourceAmount; }

  inline void SetGarrisonedInsideObject(ClientObject* object) { garrisonedObject = object; }
  inline ClientObject* GetGarrisonedInsideObject() const { return garrisonedObject; }
  
  /// Updates the unit's state to the given server time.
  void UpdateGameState(double serverTime, Map* map, Match* match);
  
 private:
  void UpdateMapCoord(double serverTime, Map* map, Match* match);
  int GetDirection(double serverTime);
  
  
  UnitType type;
  
  /// Current position of the unit sprite's center on the map.
  QPointF mapCoord;
  
  /// Directions are from 0 to kNumFacingDirections - 1.
  /// Direction 0 is to the right, increasing the direction successively rotates the unit in clockwise direction.
  int direction;
  
  /// Direction overriding the standard one. This is used for a short time after we got an unexpected movement
  /// for the unit from the server.
  int overrideDirection;
  
  /// If this is in the past, then overrideDirection must be ignored.
  double overrideDirectionExpireTime = -1;
  
  /// The object in which the unit is garrisoned, or nullptr if not garrisoned.
  ClientObject* garrisonedObject = nullptr;
  
  UnitAnimation currentAnimation;
  int currentAnimationVariant;
  double lastAnimationStartTime;
  double idleBlockedStartTime = -1;
  
  /// Represents a segment of linear unit movement.
  struct MovementSegment {
    inline MovementSegment(double serverTime, const QPointF& startPoint, const QPointF& speed, UnitAction action)
        : serverTime(serverTime),
          startPoint(startPoint),
          speed(speed),
          action(action) {}
    
    /// The server time at which the unit starts moving from startPoint.
    double serverTime;
    
    /// The start point of the movement.
    QPointF startPoint;
    
    /// The direction & speed vector of movement. This may be zero, which means
    /// that the unit stops moving at startPoint at the given serverTime.
    QPointF speed;
    
    /// The unit's action, affecting the animation used and even the interpretation of the
    /// movement: for example, for the "Build" action, the unit stays in place even though
    /// a speed is given (which in this case only indicates the unit's facing direction).
    UnitAction action;
  };
  
  /// Current movement segment of the unit.
  /// TODO: Move the attributes of this struct directly into the ClientUnit now that
  ///       we only store one segment at a time?
  MovementSegment movementSegment;
  
  // For villagers: carried resource type.
  ResourceType carriedResourceType = ResourceType::NumTypes;
  // For villagers: carried resource amount.
  u8 carriedResourceAmount = 0;
};

/// Convenience function to cast a ClientUnit to a ClientObject.
/// Before using this, you must ensure that object->isUnit().
inline ClientUnit* AsUnit(ClientObject* object) {
  return static_cast<ClientUnit*>(object);
}
inline const ClientUnit* AsUnit(const ClientObject* object) {
  return static_cast<const ClientUnit*>(object);
}
