// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/building.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/mod_manager.hpp"
#include "FreeAge/client/opengl.hpp"

bool ClientBuildingType::Load(BuildingType type, const std::filesystem::path& graphicsSubPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes) {
  this->type = type;
  
  sprites.resize(static_cast<int>(BuildingSprite::NumSprites));
  for (int spriteInt = 0; spriteInt < static_cast<int>(BuildingSprite::NumSprites); ++ spriteInt) {
    BuildingSprite spriteType = static_cast<BuildingSprite>(spriteInt);
    
    QString filename;
    switch (spriteType) {
    case BuildingSprite::Foundation: filename = GetFoundationFilename(); break;
    case BuildingSprite::Building: filename = GetFilename(); break;
    case BuildingSprite::Destruction: filename = GetDestructionFilename(); break;
    case BuildingSprite::Rubble: filename = GetRubbleFilename(); break;
    case BuildingSprite::NumSprites: LOG(ERROR) << "Invalid building sprite type";
    }
    
    if (filename.isEmpty()) {
      sprites[spriteInt] = nullptr;
    } else {
      sprites[spriteInt] = SpriteManager::Instance().GetOrLoad(
          GetModdedPath(graphicsSubPath / filename.toStdString()).string().c_str(),
          (cachePath / filename.toStdString()).string().c_str(),
          colorDilationShader,
          palettes);
      if (!sprites[spriteInt]) {
        return false;
      }
    }
  }
  
  const auto& buildingSprite = sprites[static_cast<int>(BuildingSprite::Building)]->sprite;
  maxCenterY = 0;
  for (int frame = 0; frame < buildingSprite.NumFrames(); ++ frame) {
    maxCenterY = std::max(maxCenterY, buildingSprite.frame(frame).graphic.centerY);
  }
  
  std::filesystem::path ingameTexturesSubPath = std::filesystem::path("widgetui") / "textures" / "ingame";
  std::filesystem::path iconFilename = GetIconFilename();
  if (!iconFilename.empty()) {
    iconTexture.Load(GetModdedPath(ingameTexturesSubPath / iconFilename), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  }
  
  doesCauseOutlines = DoesCauseOutlinesInternal();
  
  return true;
}

ClientBuildingType::~ClientBuildingType() {
  for (SpriteAndTextures* sprite : sprites) {
    if (sprite) {
      SpriteManager::Instance().Dereference(sprite);
    }
  }
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

QString ClientBuildingType::GetDestructionFilename() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return QStringLiteral("b_dark_town_center_age1_destruction_x1.smx");
  case BuildingType::TownCenterBack:   return QStringLiteral("");
  case BuildingType::TownCenterCenter: return QStringLiteral("");
  case BuildingType::TownCenterFront:  return QStringLiteral("");
  case BuildingType::TownCenterMain:   return QStringLiteral("");
  case BuildingType::House:            return QStringLiteral("b_dark_house_age1_destruction_x1.smx");
  case BuildingType::Mill:             return QStringLiteral("b_dark_mill_age1_destruction_x1.smx");
  case BuildingType::MiningCamp:       return QStringLiteral("b_asia_mining_camp_age2_destruction_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::LumberCamp:       return QStringLiteral("b_asia_lumber_camp_age2_destruction_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::Dock:             return QStringLiteral("");  // TODO
  case BuildingType::Barracks:         return QStringLiteral("b_dark_barracks_age1_destruction_x1.smx");
  case BuildingType::Outpost:          return QStringLiteral("b_dark_outpost_age1_destruction_x1.smx");
  case BuildingType::PalisadeWall:     return QStringLiteral("");  // TODO
  case BuildingType::PalisadeGate:     return QStringLiteral("");  // TODO
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

QString ClientBuildingType::GetRubbleFilename() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return QStringLiteral("b_dark_town_center_age1_rubble_x1.smx");
  case BuildingType::TownCenterBack:   return QStringLiteral("");
  case BuildingType::TownCenterCenter: return QStringLiteral("");
  case BuildingType::TownCenterFront:  return QStringLiteral("");
  case BuildingType::TownCenterMain:   return QStringLiteral("");
  case BuildingType::House:            return QStringLiteral("b_dark_house_age1_rubble_x1.smx");
  case BuildingType::Mill:             return QStringLiteral("b_dark_mill_age1_rubble_x1.smx");
  case BuildingType::MiningCamp:       return QStringLiteral("b_asia_mining_camp_age2_rubble_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::LumberCamp:       return QStringLiteral("b_asia_lumber_camp_age2_rubble_x1.smx");  // TODO: Could not find an "age1" variant of this
  case BuildingType::Dock:             return QStringLiteral("");  // TODO
  case BuildingType::Barracks:         return QStringLiteral("b_dark_barracks_age1_rubble_x1.smx");
  case BuildingType::Outpost:          return QStringLiteral("b_dark_outpost_age1_rubble_x1.smx");
  case BuildingType::PalisadeWall:     return QStringLiteral("");  // TODO
  case BuildingType::PalisadeGate:     return QStringLiteral("");  // TODO
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

bool ClientBuildingType::DoesCauseOutlinesInternal() const {
  // TODO: Load this from some data file
  
  switch (type) {
  case BuildingType::TownCenter:       return true;
  case BuildingType::TownCenterBack:   return true;
  case BuildingType::TownCenterCenter: return true;
  case BuildingType::TownCenterFront:  return true;
  case BuildingType::TownCenterMain:   return true;
  case BuildingType::House:            return true;
  case BuildingType::Mill:             return true;
  case BuildingType::MiningCamp:       return true;
  case BuildingType::LumberCamp:       return true;
  case BuildingType::Dock:             return true;
  case BuildingType::Barracks:         return true;
  case BuildingType::Outpost:          return true;
  case BuildingType::PalisadeWall:     return true;
  case BuildingType::PalisadeGate:     return true;
  case BuildingType::TreeOak:          return true;
  case BuildingType::ForageBush:       return false;
  case BuildingType::GoldMine:         return false;
  case BuildingType::StoneMine:        return false;
  case BuildingType::NumBuildings:
    LOG(ERROR) << "Invalid type given: BuildingType::NumBuildings";
    break;
  }
  
  return false;
}

bool ClientBuildingType::UsesRandomSpriteFrame() const {
  return IsTree(type) ||
         type == BuildingType::House ||
         type == BuildingType::PalisadeWall ||
         type == BuildingType::ForageBush ||
         type == BuildingType::GoldMine ||
         type == BuildingType::StoneMine;
}

float ClientBuildingType::GetHealthBarHeightAboveCenter(int frameIndex) const {
  constexpr float kHealthBarOffset = 25;
  
  if (UsesRandomSpriteFrame()) {
    return sprites[static_cast<int>(BuildingSprite::Building)]->sprite.frame(frameIndex).graphic.centerY + kHealthBarOffset;
  } else {
    return maxCenterY + kHealthBarOffset;
  }
}

void ClientBuildingType::SetCommandButtons(CommandButton commandButtons[3][5]) {
  // TODO: Load this from some data file
  
  if (type == BuildingType::TownCenter) {
    // NOTE: Choosing the male villager icon here follows the original game.
    //       Maybe we can increase the diversity :)
    commandButtons[0][0].SetProduceUnit(UnitType::MaleVillager, Qt::Key_A);
    
    // TODO: Set loom button
    // TODO: Set age-up button
  } else if (type == BuildingType::Barracks) {
    commandButtons[0][0].SetProduceUnit(UnitType::Militia, Qt::Key_A);
  }
}

float ClientBuilding::TreeScale = 1.f;

ClientBuilding::ClientBuilding(int playerIndex, BuildingType type, int baseTileX, int baseTileY, float buildPercentage, u32 hp)
    : ClientObject(ObjectType::Building, playerIndex, hp),
      type(type),
      fixedFrameIndex(-1),
      baseTileX(baseTileX),
      baseTileY(baseTileY),
      buildPercentage(buildPercentage) {}

QPointF ClientBuilding::GetCenterMapCoord() const {
  QSize size = GetBuildingSize(type);
  return QPointF(baseTileX + 0.5f * size.width(), baseTileY + 0.5f * size.height());
}

QRectF ClientBuilding::GetRectInProjectedCoords(
    Map* map,
    double elapsedSeconds,
    bool shadow,
    bool outline) {
  BuildingSprite spriteType = (buildPercentage < 100) ? BuildingSprite::Foundation : BuildingSprite::Building;
  const Sprite& sprite = GetClientBuildingType(type).GetSprites()[static_cast<int>(spriteType)]->sprite;
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(GetCenterMapCoord());
  int frameIndex = GetFrameIndex(elapsedSeconds);
  
  const Sprite::Frame::Layer& layer = shadow ? sprite.frame(frameIndex).shadow : sprite.frame(frameIndex).graphic;
  bool isGraphic = !shadow && !outline;
  float scale = IsTree(type) ? TreeScale : 1.f;
  return QRectF(
      centerProjectedCoord.x() - layer.centerX * scale + (isGraphic ? 1 : 0),
      centerProjectedCoord.y() - layer.centerY * scale + (isGraphic ? 1 : 0),
      layer.imageWidth * scale + (isGraphic ? -2 : 0),
      layer.imageHeight * scale + (isGraphic ? -2 : 0));
}

void ClientBuilding::Render(
    Map* map,
    QRgb outlineOrModulationColor,
    SpriteShader* spriteShader,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    double elapsedSeconds,
    bool shadow,
    bool outline) {
  const ClientBuildingType& buildingType = GetClientBuildingType(type);
  
  BuildingSprite spriteType = (buildPercentage < 100) ? BuildingSprite::Foundation : BuildingSprite::Building;
  const auto& sprite = buildingType.GetSprites()[static_cast<int>(spriteType)];
  Texture& texture = (shadow ? sprite->shadowTexture : sprite->graphicTexture);
  
  int frameIndex = GetFrameIndex(elapsedSeconds);
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(GetCenterMapCoord());
  
  if (type == BuildingType::TownCenter && spriteType == BuildingSprite::Building) {
    // Special case for town centers: Render all of their separate parts.
    // Main
    const ClientBuildingType& helperType1 = GetClientBuildingType(BuildingType::TownCenterMain);
    const auto& helperSprite1 = helperType1.GetSprites()[static_cast<int>(BuildingSprite::Building)];
    DrawSprite(
        helperSprite1->sprite,
        shadow ? helperSprite1->shadowTexture : helperSprite1->graphicTexture,
        spriteShader, centerProjectedCoord,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        outlineOrModulationColor, playerIndex, 1.f);
    
    // Back
    const ClientBuildingType& helperType2 = GetClientBuildingType(BuildingType::TownCenterBack);
    const auto& helperSprite2 = helperType2.GetSprites()[static_cast<int>(BuildingSprite::Building)];
    DrawSprite(
        helperSprite2->sprite,
        shadow ? helperSprite2->shadowTexture : helperSprite2->graphicTexture,
        spriteShader, centerProjectedCoord,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        outlineOrModulationColor, playerIndex, 1.f);
    
    // Center
    const ClientBuildingType& helperType3 = GetClientBuildingType(BuildingType::TownCenterCenter);
    const auto& helperSprite3 = helperType3.GetSprites()[static_cast<int>(BuildingSprite::Building)];
    DrawSprite(
        helperSprite3->sprite,
        shadow ? helperSprite3->shadowTexture : helperSprite3->graphicTexture,
        spriteShader, centerProjectedCoord,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        outlineOrModulationColor, playerIndex, 1.f);
  }
  
  bool useDarkModulation = spriteType == BuildingSprite::Foundation && buildPercentage == 0 && !shadow && !outline;
  if (useDarkModulation) {
    outlineOrModulationColor = qRgb(127, 127, 127);
  }
  DrawSprite(
      sprite->sprite,
      texture,
      spriteShader,
      centerProjectedCoord,
      viewMatrix,
      zoom,
      widgetWidth,
      widgetHeight,
      frameIndex,
      shadow,
      outline,
      outlineOrModulationColor,
      playerIndex,
      IsTree(type) ? TreeScale : 1.f);
  
  if (type == BuildingType::TownCenter && spriteType == BuildingSprite::Building) {
    // Front
    const ClientBuildingType& helperType4 = GetClientBuildingType(BuildingType::TownCenterFront);
    const auto& helperSprite4 = helperType4.GetSprites()[static_cast<int>(BuildingSprite::Building)];
    DrawSprite(
        helperSprite4->sprite,
        shadow ? helperSprite4->shadowTexture : helperSprite4->graphicTexture,
        spriteShader, centerProjectedCoord,
        viewMatrix, zoom, widgetWidth, widgetHeight, frameIndex, shadow, outline,
        outlineOrModulationColor, playerIndex, 1.f);
  }
}

const Sprite& ClientBuilding::GetSprite() {
  BuildingSprite spriteType = (buildPercentage < 100) ? BuildingSprite::Foundation : BuildingSprite::Building;
  const auto& sprite = GetClientBuildingType(type).GetSprites()[static_cast<int>(spriteType)];
  
  return sprite->sprite;
}

Texture& ClientBuilding::GetTexture(bool shadow) {
  BuildingSprite spriteType = (buildPercentage < 100) ? BuildingSprite::Foundation : BuildingSprite::Building;
  const auto& sprite = GetClientBuildingType(type).GetSprites()[static_cast<int>(spriteType)];
  return (shadow ? sprite->shadowTexture : sprite->graphicTexture);
}

int ClientBuilding::GetFrameIndex(double elapsedSeconds) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(type)];
  
  bool isFoundation = buildPercentage < 100;
  if (isFoundation) {
    const auto& foundationSprite = buildingType.GetSprites()[static_cast<int>(BuildingSprite::Foundation)]->sprite;
    return std::max<int>(0, std::min<int>(foundationSprite.NumFrames() - 1,
                                          (buildPercentage / 100.f) * foundationSprite.NumFrames()));
  }
  
  const auto& buildingSprite = buildingType.GetSprites()[static_cast<int>(BuildingSprite::Building)]->sprite;
  if (buildingType.UsesRandomSpriteFrame()) {
    if (fixedFrameIndex < 0) {
      fixedFrameIndex = rand() % buildingSprite.NumFrames();
    }
    return fixedFrameIndex;
  } else {
    return static_cast<int>(animationFramesPerSecond * elapsedSeconds + 0.5f) % buildingSprite.NumFrames();
  }
}

void ClientBuilding::DequeueUnit(int index) {
  if (index >= static_cast<int>(productionQueue.size())) {
    LOG(ERROR) << "Invalid production queue index given.";
    return;
  }
  
  productionQueue.erase(productionQueue.begin() + index);
  if (index == 0) {
    productionPercentage = 0;
    productionProgressPerSecond = 0;
  }
}
