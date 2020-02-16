#include "FreeAge/client_building.h"

#include "FreeAge/logging.h"
#include "FreeAge/map.h"
#include "FreeAge/opengl.h"

QSize GetBuildingSize(BuildingType type) {
  // TODO: Load this from some data file?
  
  if (static_cast<int>(type) >= static_cast<int>(BuildingType::FirstTree) &&
      static_cast<int>(type) <= static_cast<int>(BuildingType::LastTree)) {
    return QSize(1, 1);
  }
  
  switch (type) {
  case BuildingType::TownCenter:
    return QSize(4, 4);
  case BuildingType::House:
    return QSize(2, 2);
  default:
    LOG(ERROR) << "Invalid type given: " << static_cast<int>(type);
    break;
  }
  
  return QSize(0, 0);
}

QString GetBuildingFilename(BuildingType type) {
  // TODO: Load this from some data file?
  
  switch (type) {
  case BuildingType::TownCenter:       return QStringLiteral("b_dark_town_center_age1_x1.smx");
  case BuildingType::TownCenterBack:   return QStringLiteral("b_dark_town_center_age1_back_x1.smx");
  case BuildingType::TownCenterCenter: return QStringLiteral("b_dark_town_center_age1_center_x1.smx");
  case BuildingType::TownCenterFront:  return QStringLiteral("b_dark_town_center_age1_front_x1.smx");
  case BuildingType::TownCenterMain:   return QStringLiteral("b_dark_town_center_age1_main_x1.smx");
  case BuildingType::House:            return QStringLiteral("b_dark_house_age1_x1.smx");
  case BuildingType::TreeOak:          return QStringLiteral("n_tree_oak_x1.smx");
  case BuildingType::NumBuildings:
    LOG(ERROR) << "Invalid type given: BuildingType::NumBuildings";
    break;
  }
  
  return QString();
}

bool BuildingUsesRandomSpriteFrame(BuildingType type) {
  return (static_cast<int>(type) >= static_cast<int>(BuildingType::FirstTree) &&
          static_cast<int>(type) <= static_cast<int>(BuildingType::LastTree));
}

ClientBuilding::ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY)
    : playerIndex(playerIndex),
      type(type),
      fixedFrameIndex(-1),
      baseTileX(baseTileX),
      baseTileY(baseTileY) {}

QRectF ClientBuilding::GetRectInProjectedCoords(
    Map* map,
    const std::vector<Sprite>& buildingSprites,
    double elapsedSeconds,
    bool shadow,
    bool outline) {
  const Sprite& sprite = buildingSprites[static_cast<int>(type)];
  
  QSize size = GetBuildingSize(type);
  QPointF centerMapCoord = QPointF(baseTileX + 0.5f * size.width(), baseTileY + 0.5f * size.height());
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(centerMapCoord);
  
  int frameIndex = GetFrameIndex(sprite, elapsedSeconds);
  
  const Sprite::Frame::Layer& layer = shadow ? sprite.frame(frameIndex).shadow : sprite.frame(frameIndex).graphic;
  bool isGraphic = !shadow && !outline;
  return QRectF(
      centerProjectedCoord.x() - layer.centerX + (isGraphic ? 1 : 0),
      centerProjectedCoord.y() - layer.centerY + (isGraphic ? 1 : 0),
      layer.imageWidth + (isGraphic ? -2 : 0),
      layer.imageHeight + (isGraphic ? -2 : 0));
}

void ClientBuilding::Render(
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
    bool outline) {
  const Sprite& sprite = buildingSprites[static_cast<int>(type)];
  const Texture& texture = buildingTextures[static_cast<int>(type)];
  
  QSize size = GetBuildingSize(type);
  QPointF centerMapCoord = QPointF(baseTileX + 0.5f * size.width(), baseTileY + 0.5f * size.height());
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(centerMapCoord);
  
  int frameIndex = GetFrameIndex(sprite, elapsedSeconds);
  
  if (type == BuildingType::TownCenter) {
    // Special case for town centers: Render all of their separate parts.
    // Main
    DrawSprite(
        buildingSprites[static_cast<int>(BuildingType::TownCenterMain)],
        buildingTextures[static_cast<int>(BuildingType::TownCenterMain)],
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
    
    // Back
    DrawSprite(
        buildingSprites[static_cast<int>(BuildingType::TownCenterBack)],
        buildingTextures[static_cast<int>(BuildingType::TownCenterBack)],
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
    
    // Center
    DrawSprite(
        buildingSprites[static_cast<int>(BuildingType::TownCenterCenter)],
        buildingTextures[static_cast<int>(BuildingType::TownCenterCenter)],
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
  }
  
  DrawSprite(
      sprite,
      texture,
      spriteShader,
      centerProjectedCoord,
      pointBuffer,
      viewMatrix,
      zoom,
      widgetWidth,
      widgetHeight,
      frameIndex,
      shadow,
      outline,
      playerColors,
      playerIndex);
  
  if (type == BuildingType::TownCenter) {
    // Front
    DrawSprite(
        buildingSprites[static_cast<int>(BuildingType::TownCenterFront)],
        buildingTextures[static_cast<int>(BuildingType::TownCenterFront)],
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
  }
}

int ClientBuilding::GetFrameIndex(const Sprite& sprite, double elapsedSeconds) {
  if (BuildingUsesRandomSpriteFrame(type)) {
    if (fixedFrameIndex < 0) {
      fixedFrameIndex = rand() % sprite.NumFrames();
    }
    return fixedFrameIndex;
  } else {
    float framesPerSecond = 30.f;
    return static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % sprite.NumFrames();
  }
}
