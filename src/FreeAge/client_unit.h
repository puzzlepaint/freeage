#pragma once

#include <vector>

#include "FreeAge/sprite.h"
#include "FreeAge/shader_sprite.h"
#include "FreeAge/texture.h"

class Map;


constexpr int kNumFacingDirections = 16;

/// Unit types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector.
enum class UnitType {
  FemaleVillager,
  MaleVillager,
  Scout,
  
  NumUnits
};


enum class UnitAnimation {
  Idle = 0,
  NumAnimationTypes
};


struct SpriteAndTextures {
  Sprite sprite;
  Texture graphicTexture;
  Texture shadowTexture;
};


/// Stores client-side data for unit types (i.e., their graphics).
class ClientUnitType {
 public:
  ClientUnitType() = default;
  
  bool Load(UnitType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes);
  
  int GetHealthBarHeightAboveCenter() const;
  
  inline const std::vector<SpriteAndTextures>& GetAnimations(UnitAnimation type) const { return animations[static_cast<int>(type)]; }
  
 private:
  bool LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes, UnitAnimation type);
  
  /// Indexed by: [static_cast<int>(UnitAnimation animation)][animation_variant]
  std::vector<std::vector<SpriteAndTextures>> animations;
  
  /// The maximum centerY value of any graphic frame of this unit type in the idle animation(s) when facing right.
  /// This can be used to determine a reasonable height for the unit's health bar.
  int maxCenterY;
};


/// Represents a unit on the client side.
class ClientUnit {
 public:
  ClientUnit(int playerIndex, UnitType type, const QPointF& mapCoord);
  
  /// Returns the projected coordinates of this unit's center point.
  QPointF GetCenterProjectedCoord(Map* map);
  
  /// Computes the sprite rectangle for this unit in projected coordinates.
  QRectF GetRectInProjectedCoords(
      Map* map,
      const std::vector<ClientUnitType>& unitTypes,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  void Render(
      Map* map,
      const std::vector<ClientUnitType>& unitTypes,
      const std::vector<QRgb>& playerColors,
      SpriteShader* spriteShader,
      GLuint pointBuffer,
      float* viewMatrix,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  inline int GetPlayerIndex() const { return playerIndex; }
  inline UnitType GetType() const { return type; }
  inline UnitAnimation GetCurrentAnimation() const { return currentAnimation; }
  
  inline const QPointF& GetMapCoord() const { return mapCoord; }
  
 private:
  int playerIndex;
  UnitType type;
  
  bool isSelected;
  
  /// Current position of the unit sprite's center on the map.
  QPointF mapCoord;
  
  /// Directions are from 0 to kNumFacingDirections - 1.
  /// Direction 0 is to the right, increasing the direction successively rotates the unit in clockwise direction.
  int direction;
  
  UnitAnimation currentAnimation;
  int currentAnimationVariant;
  double lastAnimationStartTime;
};
