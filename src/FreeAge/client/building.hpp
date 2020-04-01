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


enum class BuildingSprite {
  Foundation = 0,
  Building,
  Destruction,
  Rubble,
  NumSprites
};

/// Stores client-side data for building types (i.e., their graphics).
/// Access the global unit types vector via GetBuildingTypes().
class ClientBuildingType {
 public:
  ClientBuildingType() = default;
  ~ClientBuildingType();
  
  bool Load(BuildingType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes);
  
  QSize GetSize() const;
  bool UsesRandomSpriteFrame() const;
  /// Returns the height (in projected coordinates) above the building's center at which the health bar should be displayed.
  float GetHealthBarHeightAboveCenter(int frameIndex) const;
  
  /// Sets up the command buttons for the actions that can be performed when this building type (only) is selected.
  void SetCommandButtons(CommandButton commandButtons[3][5]);
  
  const std::vector<SpriteAndTextures*>& GetSprites() const { return sprites; }
  
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
  QString GetDestructionFilename() const;
  QString GetRubbleFilename() const;
  std::filesystem::path GetIconFilename() const;
  bool DoesCauseOutlinesInternal() const;
  
  BuildingType type;
  
  /// Indexed by: [static_cast<int>(BuildingSprite sprite)]
  std::vector<SpriteAndTextures*> sprites;
  
  /// The maximum centerY value of any graphic frame of this building type.
  /// For animated buildings such as mills, this can be used to determine a reasonable
  /// height for the building's health bar.
  int maxCenterY;
  
  Texture iconTexture;
  
  bool doesCauseOutlines;
};

/// Convenience function that returns the ClientBuildingType for a given building type.
inline ClientBuildingType& GetClientBuildingType(BuildingType type) {
  return ClientBuildingType::GetBuildingTypes()[static_cast<int>(type)];
}


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
  
  Texture& GetTexture(bool shadow);
  int GetFrameIndex(double elapsedSeconds);
  
  void Render(
      Map* map,
      QRgb outlineOrModulationColor,
      SpriteShader* spriteShader,
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
