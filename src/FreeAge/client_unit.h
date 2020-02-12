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
  
  bool Load(UnitType type, const std::filesystem::path& graphicsPath, const Palettes& palettes);
  
  inline const std::vector<SpriteAndTextures>& GetAnimations(UnitAnimation type) const { return animations[static_cast<int>(type)]; }
  
 private:
  bool LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const Palettes& palettes, UnitAnimation type);
  
  /// Indexed by: [static_cast<int>(UnitAnimation animation)][animation_variant]
  std::vector<std::vector<SpriteAndTextures>> animations;
};


/// Represents a unit on the client side.
class ClientUnit {
 public:
  ClientUnit(UnitType type, const QPointF& mapCoord);
  
  /// Computes the sprite rectangle for this unit in projected coordinates.
  QRectF GetRectInProjectedCoords(
      Map* map,
      const std::vector<ClientUnitType>& unitTypes,
      double elapsedSeconds,
      bool shadow);
  
  void Render(
      Map* map,
      const std::vector<ClientUnitType>& unitTypes,
      SpriteShader* spriteShader,
      GLuint pointBuffer,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double elapsedSeconds,
      bool shadow);
  
  inline UnitType GetType() const { return type; }
  
  inline const QPointF& GetMapCoord() const { return mapCoord; }
  
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
};
