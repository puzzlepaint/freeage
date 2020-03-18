#include "FreeAge/client/building.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/opengl.hpp"

bool ClientBuildingType::Load(BuildingType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes) {
  this->type = type;
  
  // Load main sprite
  QString filename = GetFilename();
  if (!LoadSpriteAndTexture(
      (graphicsPath / filename.toStdString()).c_str(),
      (cachePath / filename.toStdString()).c_str(),
      GL_CLAMP,
      GL_NEAREST,
      GL_NEAREST,
      &sprite,
      &texture,
      &shadowTexture,
      palettes)) {
    return false;
  }
  
  // Load foundation sprite
  QString foundationFilename = GetFoundationFilename();
  if (!foundationFilename.isEmpty()) {
    if (!LoadSpriteAndTexture(
        (graphicsPath / foundationFilename.toStdString()).c_str(),
        (cachePath / foundationFilename.toStdString()).c_str(),
        GL_CLAMP,
        GL_NEAREST,
        GL_NEAREST,
        &foundationSprite,
        &foundationTexture,
        &foundationShadowTexture,
        palettes)) {
      return false;
    }
  }
  
  maxCenterY = 0;
  for (int frame = 0; frame < sprite.NumFrames(); ++ frame) {
    maxCenterY = std::max(maxCenterY, sprite.frame(frame).graphic.centerY);
  }
  
  std::filesystem::path ingameTexturesPath =
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame";
  std::filesystem::path iconFilename = GetIconFilename();
  if (!iconFilename.empty()) {
    iconTexture.Load(ingameTexturesPath / iconFilename, GL_CLAMP, GL_LINEAR, GL_LINEAR);
  }
  
  return true;
}

QSize ClientBuildingType::GetSize() const {
  return GetBuildingSize(type);
}

QString ClientBuildingType::GetFilename() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return QStringLiteral("b_dark_town_center_age1_x1.smx");
  case BuildingType::TownCenterBack:   return QStringLiteral("b_dark_town_center_age1_back_x1.smx");
  case BuildingType::TownCenterCenter: return QStringLiteral("b_dark_town_center_age1_center_x1.smx");
  case BuildingType::TownCenterFront:  return QStringLiteral("b_dark_town_center_age1_front_x1.smx");
  case BuildingType::TownCenterMain:   return QStringLiteral("b_dark_town_center_age1_main_x1.smx");
  case BuildingType::House:            return QStringLiteral("b_dark_house_age1_x1.smx");
  case BuildingType::Mill:             return QStringLiteral("b_dark_mill_age1_x1.smx");
  case BuildingType::MiningCamp:       return QStringLiteral("b_asia_mining_camp_age2_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::LumberCamp:       return QStringLiteral("b_asia_lumber_camp_age2_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::Dock:             return QStringLiteral("b_dark_dock_age1_x1.smx");
  case BuildingType::Barracks:         return QStringLiteral("b_dark_barracks_age1_x1.smx");
  case BuildingType::Outpost:          return QStringLiteral("b_dark_outpost_age1_x1.smx");
  case BuildingType::PalisadeWall:     return QStringLiteral("b_dark_wall_palisade_x1.smx");
  case BuildingType::PalisadeGate:     return QStringLiteral("b_dark_gate_palisade_e_closed_x1.smx");  // TODO: This consists of multiple parts and has multiple orientations
  case BuildingType::TreeOak:          return QStringLiteral("n_tree_oak_x1.smx");
  case BuildingType::ForageBush:       return QStringLiteral("n_forage_bush_x1.smx");  // TODO: Partly depleted variants: n_forage_bush_66_x1.smx, n_forage_bush_33_x1.smx
  case BuildingType::GoldMine:         return QStringLiteral("n_mine_gold_x1.smx");  // TODO: Partly depleted variants: n_mine_gold_66_x1.smx, n_mine_gold_33_x1.smx
  case BuildingType::StoneMine:        return QStringLiteral("n_mine_stone_x1.smx");  // TODO: Partly depleted variants: n_mine_stone_66_x1.smx, n_mine_stone_66_x1.smx
  case BuildingType::NumBuildings:
    LOG(ERROR) << "Invalid type given: BuildingType::NumBuildings";
    break;
  }
  
  return QString();
}

QString ClientBuildingType::GetFoundationFilename() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return QStringLiteral("b_misc_foundation_town_center_x1.smx");
  case BuildingType::TownCenterBack:   return QStringLiteral("");
  case BuildingType::TownCenterCenter: return QStringLiteral("");
  case BuildingType::TownCenterFront:  return QStringLiteral("");
  case BuildingType::TownCenterMain:   return QStringLiteral("");
  case BuildingType::House:            return QStringLiteral("b_misc_foundation_house_x1.smx");
  case BuildingType::Mill:             return QStringLiteral("b_misc_foundation_mill_x1.smx");
  case BuildingType::MiningCamp:       return QStringLiteral("b_misc_foundation_mining_camp_x1.smx");
  case BuildingType::LumberCamp:       return QStringLiteral("b_misc_foundation_lumber_camp_x1.smx");
  case BuildingType::Dock:             return QStringLiteral("b_misc_foundation_dock_x1.smx");
  case BuildingType::Barracks:         return QStringLiteral("b_misc_foundation_barracks_x1.smx");
  case BuildingType::Outpost:          return QStringLiteral("b_misc_foundation_outpost_x1.smx");
  case BuildingType::PalisadeWall:     return QStringLiteral("b_misc_foundation_1x1_x1.smx");  // TODO: Is this correct?
  case BuildingType::PalisadeGate:     return QStringLiteral("b_dark_gate_palisade_e_constr_x1.smx");  // TODO: This has multiple orientations
  case BuildingType::TreeOak:          return QStringLiteral("");
  case BuildingType::ForageBush:       return QStringLiteral("");
  case BuildingType::GoldMine:         return QStringLiteral("");
  case BuildingType::StoneMine:        return QStringLiteral("");
  case BuildingType::NumBuildings:
    LOG(ERROR) << "Invalid type given: BuildingType::NumBuildings";
    break;
  }
  
  return QString();
}

std::filesystem::path ClientBuildingType::GetIconFilename() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return std::filesystem::path("buildings") / "028_town_center.DDS";
  case BuildingType::TownCenterBack:   return std::filesystem::path();
  case BuildingType::TownCenterCenter: return std::filesystem::path();
  case BuildingType::TownCenterFront:  return std::filesystem::path();
  case BuildingType::TownCenterMain:   return std::filesystem::path();
  case BuildingType::House:            return std::filesystem::path("buildings") / "034_house.DDS";
  case BuildingType::Mill:             return std::filesystem::path("buildings") / "019_mill_1.DDS";
  case BuildingType::MiningCamp:       return std::filesystem::path("buildings") / "039_mining_camp.DDS";
  case BuildingType::LumberCamp:       return std::filesystem::path("buildings") / "040_lumber_camp.DDS";
  case BuildingType::Dock:             return std::filesystem::path("buildings") / "013_dock_1.DDS";
  case BuildingType::Barracks:         return std::filesystem::path("buildings") / "002_barracks_1.DDS";
  case BuildingType::Outpost:          return std::filesystem::path("buildings") / "038_outpost.DDS";
  case BuildingType::PalisadeWall:     return std::filesystem::path("buildings") / "030_palisade.DDS";
  case BuildingType::PalisadeGate:     return std::filesystem::path("buildings") / "044_palisade_gate.DDS";
  case BuildingType::TreeOak:          return std::filesystem::path("units") / "032_50730.DDS";
  case BuildingType::ForageBush:       return std::filesystem::path("units") / "006_50730.DDS";
  case BuildingType::GoldMine:         return std::filesystem::path("units") / "014_50730.DDS";
  case BuildingType::StoneMine:        return std::filesystem::path("units") / "009_50730.DDS";
  case BuildingType::NumBuildings:
    LOG(ERROR) << "Invalid type given: BuildingType::NumBuildings";
    break;
  }
  
  return std::filesystem::path();
}

bool ClientBuildingType::UsesRandomSpriteFrame() const {
  return (static_cast<int>(type) >= static_cast<int>(BuildingType::FirstTree) &&
          static_cast<int>(type) <= static_cast<int>(BuildingType::LastTree)) ||
         type == BuildingType::House ||
         type == BuildingType::PalisadeWall ||
         type == BuildingType::ForageBush ||
         type == BuildingType::GoldMine ||
         type == BuildingType::StoneMine;
}

float ClientBuildingType::GetHealthBarHeightAboveCenter(int frameIndex) const {
  constexpr float kHealthBarOffset = 25;
  
  if (UsesRandomSpriteFrame()) {
    return sprite.frame(frameIndex).graphic.centerY + kHealthBarOffset;
  } else {
    return maxCenterY + kHealthBarOffset;
  }
}

void ClientBuildingType::SetCommandButtons(CommandButton commandButtons[3][5]) {
  // TODO: Load this from some data file
  
  if (type == BuildingType::TownCenter) {
    // NOTE: Choosing the male villager icon here follows the original game.
    //       Maybe we can increase the diversity :)
    commandButtons[0][0].SetProduceUnit(UnitType::MaleVillager);
    
    // TODO: Set loom button
    // TODO: Set age-up button
  } else if (type == BuildingType::Barracks) {
    commandButtons[0][0].SetProduceUnit(UnitType::Militia);
  }
}


ClientBuilding::ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY, float buildPercentage, u32 hp)
    : ClientObject(ObjectType::Building, playerIndex, hp),
      type(type),
      fixedFrameIndex(-1),
      baseTileX(baseTileX),
      baseTileY(baseTileY),
      buildPercentage(buildPercentage) {}

QPointF ClientBuilding::GetCenterProjectedCoord(
    Map* map) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(type)];
  
  QSize size = buildingType.GetSize();
  QPointF centerMapCoord = QPointF(baseTileX + 0.5f * size.width(), baseTileY + 0.5f * size.height());
  return map->MapCoordToProjectedCoord(centerMapCoord);
}

QRectF ClientBuilding::GetRectInProjectedCoords(
    Map* map,
    double elapsedSeconds,
    bool shadow,
    bool outline) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(type)];
  
  bool isFoundation = buildPercentage < 100;
  const Sprite& sprite = isFoundation ? buildingType.GetFoundationSprite() : buildingType.GetSprite();
  QPointF centerProjectedCoord = GetCenterProjectedCoord(map);
  int frameIndex = GetFrameIndex(buildingType, elapsedSeconds);
  
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
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(type)];
  
  bool isFoundation = buildPercentage < 100;
  
  const Texture& texture =
      isFoundation ?
      (shadow ? buildingType.GetFoundationShadowTexture() : buildingType.GetFoundationTexture()) :
      (shadow ? buildingType.GetShadowTexture() : buildingType.GetTexture());
  
  int frameIndex = GetFrameIndex(buildingType, elapsedSeconds);
  QPointF centerProjectedCoord = GetCenterProjectedCoord(map);
  
  if (type == BuildingType::TownCenter && !isFoundation) {
    // Special case for town centers: Render all of their separate parts.
    // Main
    const ClientBuildingType& helperType1 = buildingTypes[static_cast<int>(BuildingType::TownCenterMain)];
    DrawSprite(
        helperType1.GetSprite(),
        shadow ? helperType1.GetShadowTexture() : helperType1.GetTexture(),
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
    
    // Back
    const ClientBuildingType& helperType2 = buildingTypes[static_cast<int>(BuildingType::TownCenterBack)];
    DrawSprite(
        helperType2.GetSprite(),
        shadow ? helperType2.GetShadowTexture() : helperType2.GetTexture(),
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
    
    // Center
    const ClientBuildingType& helperType3 = buildingTypes[static_cast<int>(BuildingType::TownCenterCenter)];
    DrawSprite(
        helperType3.GetSprite(),
        shadow ? helperType3.GetShadowTexture() : helperType3.GetTexture(),
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
  }
  
  bool useDarkModulation = isFoundation && buildPercentage == 0 && !shadow && !outline;
  if (useDarkModulation) {
    spriteShader->GetProgram()->UseProgram();
    spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 0.5, 0.5, 0.5, 1);
  }
  DrawSprite(
      isFoundation ? buildingType.GetFoundationSprite() : buildingType.GetSprite(),
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
  if (useDarkModulation) {
    spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 1, 1, 1, 1);
  }
  
  if (type == BuildingType::TownCenter && !isFoundation) {
    // Front
    const ClientBuildingType& helperType4 = buildingTypes[static_cast<int>(BuildingType::TownCenterFront)];
    DrawSprite(
        helperType4.GetSprite(),
        shadow ? helperType4.GetShadowTexture() : helperType4.GetTexture(),
        spriteShader, centerProjectedCoord, pointBuffer,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        playerColors, playerIndex);
  }
}

const Sprite& ClientBuilding::GetSprite() {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(type)];
  
  bool isFoundation = buildPercentage < 100;
  
  return isFoundation ? buildingType.GetFoundationSprite() : buildingType.GetSprite();
}

int ClientBuilding::GetFrameIndex(
    const ClientBuildingType& buildingType,
    double elapsedSeconds) {
  bool isFoundation = buildPercentage < 100;
  if (isFoundation) {
    return std::max<int>(0, std::min<int>(buildingType.GetFoundationSprite().NumFrames() - 1,
                                          (buildPercentage / 100.f) * buildingType.GetFoundationSprite().NumFrames()));
  }
  
  if (buildingType.UsesRandomSpriteFrame()) {
    if (fixedFrameIndex < 0) {
      fixedFrameIndex = rand() % buildingType.GetSprite().NumFrames();
    }
    return fixedFrameIndex;
  } else {
    float framesPerSecond = 30.f;
    return static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % buildingType.GetSprite().NumFrames();
  }
}
