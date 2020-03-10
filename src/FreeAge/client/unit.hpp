#pragma once

#include <vector>

#include "FreeAge/common/unit_types.hpp"
#include "FreeAge/client/object.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/texture.hpp"

class Map;


constexpr int kNumFacingDirections = 16;

enum class UnitAnimation {
  Idle = 0,
  Walk,
  NumAnimationTypes
};


struct SpriteAndTextures {
  Sprite sprite;
  Texture graphicTexture;
  Texture shadowTexture;
};


/// Stores client-side data for unit types (i.e., their graphics).
/// Access the global unit types vector via GetUnitTypes().
class ClientUnitType {
 public:
  ClientUnitType() = default;
  
  bool Load(UnitType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes);
  
  int GetHealthBarHeightAboveCenter() const;
  
  inline const std::vector<SpriteAndTextures>& GetAnimations(UnitAnimation type) const { return animations[static_cast<int>(type)]; }
  
  inline const Texture* GetIconTexture() const { return &iconTexture; }
  
  /// Returns the global instance of the unit types vector.
  inline static std::vector<ClientUnitType>& GetUnitTypes() {
    static std::vector<ClientUnitType> unitTypesSingleton;
    return unitTypesSingleton;
  }
  
 private:
  bool LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes, UnitAnimation type);
  
  /// Indexed by: [static_cast<int>(UnitAnimation animation)][animation_variant]
  std::vector<std::vector<SpriteAndTextures>> animations;
  
  /// The maximum centerY value of any graphic frame of this unit type in the idle animation(s) when facing right.
  /// This can be used to determine a reasonable height for the unit's health bar.
  int maxCenterY;
  
  Texture iconTexture;
};


/// Represents a unit on the client side.
class ClientUnit : public ClientObject {
 public:
  ClientUnit(int playerIndex, UnitType type, const QPointF& mapCoord);
  
  /// Returns the projected coordinates of this unit's center point.
  QPointF GetCenterProjectedCoord(Map* map);
  
  /// Computes the sprite rectangle for this unit in projected coordinates.
  QRectF GetRectInProjectedCoords(
      Map* map,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  void Render(
      Map* map,
      const std::vector<QRgb>& playerColors,
      SpriteShader* spriteShader,
      GLuint pointBuffer,
      float* viewMatrix,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double serverTime,
      bool shadow,
      bool outline);
  
  inline UnitType GetType() const { return type; }
  inline QString GetUnitName() const { return ::GetUnitName(type); }
  inline const Texture* GetIconTexture() const { return ClientUnitType::GetUnitTypes()[static_cast<int>(type)].GetIconTexture(); };
  
  inline UnitAnimation GetCurrentAnimation() const { return currentAnimation; }
  void SetCurrentAnimation(UnitAnimation animation, double serverTime);
  
  inline const QPointF& GetMapCoord() const { return mapCoord; }
  
  void AddMovementSegment(double serverTime, const QPointF& startPoint, const QPointF& speed) {
    movementSegments.emplace_back(serverTime, startPoint, speed);
  }
  
  /// Updates the unit's state to the given server time.
  void UpdateGameState(double serverTime);
  
 private:
  UnitType type;
  
  /// Current position of the unit sprite's center on the map.
  QPointF mapCoord;
  
  /// Directions are from 0 to kNumFacingDirections - 1.
  /// Direction 0 is to the right, increasing the direction successively rotates the unit in clockwise direction.
  int direction;
  
  UnitAnimation currentAnimation;
  int currentAnimationVariant;
  double lastAnimationStartTime;
  
  /// Represents a segment of linear unit movement.
  struct MovementSegment {
    inline MovementSegment(double serverTime, const QPointF& startPoint, const QPointF& speed)
        : serverTime(serverTime),
          startPoint(startPoint),
          speed(speed) {}
    
    /// The server time at which the unit starts moving from startPoint.
    double serverTime;
    
    /// The start point of the movement.
    QPointF startPoint;
    
    /// The direction & speed vector of movement. This may be zero, which means
    /// that the unit stops moving at startPoint at the given serverTime.
    QPointF speed;
  };
  
  /// Ordered list of scheduled movement segments, sorted by increasing server time.
  /// If the unit stands still, this is empty. Otherwise, it contains at least the current
  /// segment of movement, and possibly additional segments for later.
  std::vector<MovementSegment> movementSegments;
};
