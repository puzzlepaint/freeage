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
  TownCenterBack,    // Not used as building, just for loading the sprite
  TownCenterCenter,  // Not used as building, just for loading the sprite
  TownCenterFront,   // Not used as building, just for loading the sprite
  TownCenterMain,    // Not used as building, just for loading the sprite
  House,
  
  // Gaia "buildings"
  TreeOak,
  FirstTree = TreeOak,
  LastTree = TreeOak,
  
  NumBuildings
};

QSize GetBuildingSize(BuildingType type);
QString GetBuildingFilename(BuildingType type);
bool BuildingUsesRandomSpriteFrame(BuildingType type);


/// Represents a building on the client side.
class ClientBuilding {
 public:
  ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY);
  
  /// Computes the sprite rectangle for this building in projected coordinates.
  /// If shadow is true, returns the rectangle for the shadow sprite.
  QRectF GetRectInProjectedCoords(
      Map* map,
      const std::vector<Sprite>& buildingSprites,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  void Render(
      Map* map,
      const std::vector<Sprite>& buildingSprites,
      const std::vector<Texture>& buildingTextures,
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
  
  inline BuildingType GetType() const { return type; }
  
  inline QPoint GetBaseTile() const { return QPoint(baseTileX, baseTileY); }
  
 private:
  int GetFrameIndex(const Sprite& sprite, double elapsedSeconds);
  
  
  int playerIndex;
  BuildingType type;
  
  /// In case the building uses a random but fixed frame index, it is stored here.
  int fixedFrameIndex;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  int baseTileX;
  int baseTileY;
};
