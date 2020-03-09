#pragma once

#include <QSize>
#include <QString>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/client/object.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/texture.hpp"

class Map;


/// Stores client-side data for building types (i.e., their graphics).
class ClientBuildingType {
 public:
  ClientBuildingType() = default;
  
  bool Load(BuildingType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes);
  
  QSize GetSize() const;
  bool UsesRandomSpriteFrame() const;
  /// Returns the height (in projected coordinates) above the building's center at which the health bar should be displayed.
  float GetHealthBarHeightAboveCenter(int frameIndex) const;
  
  inline const Sprite& GetSprite() const { return sprite; }
  inline const Texture& GetTexture() const { return texture; }
  inline const Texture& GetShadowTexture() const { return shadowTexture; }
  
 private:
  QString GetFilename() const;
  
  BuildingType type;
  
  Sprite sprite;
  Texture texture;
  Texture shadowTexture;
  
  /// The maximum centerY value of any graphic frame of this building type.
  /// For animated buildings such as mills, this can be used to determine a reasonable
  /// height for the building's health bar.
  int maxCenterY;
};


/// Represents a building on the client side.
class ClientBuilding : public ClientObject {
 public:
  ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY);
  
  /// Returns the projected coordinates of this building's center point.
  QPointF GetCenterProjectedCoord(
      Map* map,
      const std::vector<ClientBuildingType>& buildingTypes);
  
  /// Computes the sprite rectangle for this building in projected coordinates.
  /// If shadow is true, returns the rectangle for the shadow sprite.
  QRectF GetRectInProjectedCoords(
      Map* map,
      const std::vector<ClientBuildingType>& buildingTypes,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  int GetFrameIndex(
      const ClientBuildingType& buildingType,
      double elapsedSeconds);
  
  void Render(
      Map* map,
      const std::vector<ClientBuildingType>& buildingTypes,
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
  BuildingType type;
  
  /// In case the building uses a random but fixed frame index, it is stored here.
  int fixedFrameIndex;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  int baseTileX;
  int baseTileY;
};
