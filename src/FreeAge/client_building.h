#pragma once

#include <QSize>
#include <QString>

#include "FreeAge/shader_sprite.h"
#include "FreeAge/sprite.h"
#include "FreeAge/texture.h"

class Map;

/// Building types. The numbers must be sequential, starting from zero,
/// since they are used to index into a std::vector of Sprite.
enum class BuildingType {
  // Player buildings
  TownCenter = 0,
  TownCenterBack,  // Not used as building, just for loading the sprite
  TownCenterCenter,  // Not used as building, just for loading the sprite
  TownCenterFront,  // Not used as building, just for loading the sprite
  House,
  
  // Gaia "buildings"
  TreeOak,
  FirstTree = TreeOak,
  LastTree = TreeOak,
  
  NumBuildings
};

QSize GetBuildingSize(BuildingType type);
QString GetBuildingFilename(BuildingType type);


/// Represents a building on the client side.
class ClientBuilding {
 public:
  ClientBuilding(BuildingType type, int baseTileX, int baseTileY);
  
  void Render(
      Map* map,
      const Sprite& sprite,
      const Texture& texture,
      SpriteShader* spriteShader,
      GLuint pointBuffer,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      float elapsedSeconds) const;
  
  inline BuildingType GetType() const { return type; }
  
 private:
  BuildingType type;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  int baseTileX;
  int baseTileY;
};
