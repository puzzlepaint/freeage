#pragma once

#include <QSize>
#include <QString>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/client/command_button.hpp"
#include "FreeAge/client/object.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/texture.hpp"

class Map;


/// Stores client-side data for building types (i.e., their graphics).
/// Access the global unit types vector via GetBuildingTypes().
class ClientBuildingType {
 public:
  ClientBuildingType() = default;
  
  bool Load(BuildingType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes);
  
  QSize GetSize() const;
  bool UsesRandomSpriteFrame() const;
  /// Returns the height (in projected coordinates) above the building's center at which the health bar should be displayed.
  float GetHealthBarHeightAboveCenter(int frameIndex) const;
  
  /// Sets up the command buttons for the actions that can be performed when this building type (only) is selected.
  void SetCommandButtons(CommandButton commandButtons[3][5]);
  
  inline const Sprite& GetSprite() const { return sprite; }
  inline const Texture& GetTexture() const { return texture; }
  inline const Texture& GetShadowTexture() const { return shadowTexture; }
  
  inline const Sprite& GetFoundationSprite() const { return foundationSprite; }
  inline const Texture& GetFoundationTexture() const { return foundationTexture; }
  inline const Texture& GetFoundationShadowTexture() const { return foundationShadowTexture; }
  
  inline const Texture* GetIconTexture() const { return &iconTexture; }
  
  inline bool DoesCauseOutlines() const { return doesCauseOutlines; }
  
  /// Returns the global instance of the building types vector.
  inline static std::vector<ClientBuildingType>& GetBuildingTypes() {
    static std::vector<ClientBuildingType> buildingTypesSingleton;
    return buildingTypesSingleton;
  }
  
 private:
  QString GetFilename() const;
  QString GetFoundationFilename() const;
  std::filesystem::path GetIconFilename() const;
  bool DoesCauseOutlinesInternal() const;
  
  BuildingType type;
  
  Sprite sprite;
  Texture texture;
  Texture shadowTexture;
  
  Sprite foundationSprite;
  Texture foundationTexture;
  Texture foundationShadowTexture;
  
  /// The maximum centerY value of any graphic frame of this building type.
  /// For animated buildings such as mills, this can be used to determine a reasonable
  /// height for the building's health bar.
  int maxCenterY;
  
  Texture iconTexture;
  
  bool doesCauseOutlines;
};


/// Represents a building on the client side.
class ClientBuilding : public ClientObject {
 public:
  ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY, float buildPercentage, u32 hp);
  
  /// Returns the projected coordinates of this building's center point.
  QPointF GetCenterMapCoord();
  
  /// Computes the sprite rectangle for this building in projected coordinates.
  /// If shadow is true, returns the rectangle for the shadow sprite.
  QRectF GetRectInProjectedCoords(
      Map* map,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  /// Returns the current sprite for this building. This can differ (e.g., it could be the foundation or main sprite).
  const Sprite& GetSprite();
  
  int GetFrameIndex(
      const ClientBuildingType& buildingType,
      double elapsedSeconds);
  
  void Render(
      Map* map,
      QRgb outlineColor,
      SpriteShader* spriteShader,
      GLuint pointBuffer,
      float* viewMatrix,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double elapsedSeconds,
      bool shadow,
      bool outline);
  
  inline void SetFixedFrameIndex(int index) { fixedFrameIndex = index; }
  
  inline BuildingType GetType() const { return type; }
  inline QString GetBuildingName() const { return ::GetBuildingName(type); }
  inline const Texture* GetIconTexture() const { return ClientBuildingType::GetBuildingTypes()[static_cast<int>(type)].GetIconTexture(); };
  inline QPoint GetBaseTile() const { return QPoint(baseTileX, baseTileY); }
  
  inline float GetBuildPercentage() const { return buildPercentage; }
  inline void SetBuildPercentage(float percentage) { buildPercentage = percentage; }
  
 private:
  BuildingType type;
  
  /// In case the building uses a random but fixed frame index, it is stored here.
  int fixedFrameIndex;
  
  /// The "base tile" is the minimum map tile coordinate on which the building
  /// stands on.
  int baseTileX;
  int baseTileY;
  
  /// The build percentage of this building, in percent. Special cases:
  /// * Exactly 100 means that the building is finished.
  /// * Exactly   0 means that this is a building foundation (i.e., it does not affect map occupancy (yet)).
  float buildPercentage;
};
