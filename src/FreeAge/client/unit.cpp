#include "FreeAge/client/unit.hpp"

#include <cmath>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"

bool ClientUnitType::Load(UnitType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes) {
  animations.resize(static_cast<int>(UnitAnimation::NumAnimationTypes));
  
  std::filesystem::path ingameUnitsPath =
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame" / "units";
  
  // TODO: Automatically check how many animations (A, B) for each type are available.
  
  std::string spriteBaseName;
  std::string iconPath;
  
  bool ok = true;
  switch (type) {
  case UnitType::FemaleVillager:
    spriteBaseName = "u_vil_female_villager";
    iconPath = ingameUnitsPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerBuilder:
    spriteBaseName = "u_vil_female_builder";
    iconPath = ingameUnitsPath / "016_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::FemaleVillagerForager:
    spriteBaseName = "u_vil_female_forager";
    iconPath = ingameUnitsPath / "016_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::FemaleVillagerLumberjack:
    spriteBaseName = "u_vil_female_lumberjack";
    iconPath = ingameUnitsPath / "016_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::FemaleVillagerGoldMiner:
    spriteBaseName = "u_vil_female_miner_gold";
    iconPath = ingameUnitsPath / "016_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::FemaleVillagerStoneMiner:
    spriteBaseName = "u_vil_female_miner_gold";  // TODO: Use the same animations as for the gold miner (without loading them twice!), except for u_vil_female_miner_stone_carrywalkA_x1.smx
    iconPath = ingameUnitsPath / "016_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::MaleVillager:
    spriteBaseName = "u_vil_male_villager";
    iconPath = ingameUnitsPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerBuilder:
    spriteBaseName = "u_vil_male_builder";
    iconPath = ingameUnitsPath / "015_50730.DDS";  // TODO: Do not load the icon multiple times
    break;
  case UnitType::MaleVillagerForager:
    spriteBaseName = "u_vil_male_forager";
    iconPath = ingameUnitsPath / "015_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::MaleVillagerLumberjack:
    spriteBaseName = "u_vil_male_lumberjack";
    iconPath = ingameUnitsPath / "015_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::MaleVillagerGoldMiner:
    spriteBaseName = "u_vil_male_miner_gold";
    iconPath = ingameUnitsPath / "015_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::MaleVillagerStoneMiner:
    spriteBaseName = "u_vil_male_miner_gold";  // TODO: Use the same animations as for the gold miner (without loading them twice!), except for u_vil_male_miner_stone_carrywalkA_x1.smx
    iconPath = ingameUnitsPath / "015_50730.DDS";  // TODO: Do not load this icon multiple times
    break;
  case UnitType::Militia:
    spriteBaseName = "u_inf_militia";
    iconPath = ingameUnitsPath / "008_50730.DDS";
    break;
  case UnitType::Scout:
    spriteBaseName = "u_cav_scout";
    iconPath = ingameUnitsPath / "064_50730.DDS";
    break;
  case UnitType::NumUnits:
    LOG(ERROR) << "Invalid unit type in ClientUnitType constructor: " << static_cast<int>(type);
    ok = false;
    break;
  }
  
  auto makeSpriteFilename = [&](const std::string& animationFilename, int variant) {
    return spriteBaseName + "_" + animationFilename + static_cast<char>('A' + variant) + "_x1.smx";
  };
  
  for (int animationTypeInt = 0; animationTypeInt < static_cast<int>(UnitAnimation::NumAnimationTypes); ++ animationTypeInt) {
    UnitAnimation animationType = static_cast<UnitAnimation>(animationTypeInt);
    
    std::string animationFilenameComponent;
    switch (animationType) {
    case UnitAnimation::Idle: animationFilenameComponent = "idle"; break;
    case UnitAnimation::Walk: animationFilenameComponent = "walk"; break;
    case UnitAnimation::Task: animationFilenameComponent = "task"; break;
    case UnitAnimation::NumAnimationTypes: LOG(ERROR) << "Invalid animation type.";
    }
    
    // Determine the number of animation variants.
    int numVariants = 0;
    for (int variant = 0; variant < 99; ++ variant) {
      std::string filename = makeSpriteFilename(animationFilenameComponent, variant);
      if (std::filesystem::exists(graphicsPath / filename)) {
        ++ numVariants;
      } else {
        break;
      }
    }
    
    // Load each variant.
    animations[animationTypeInt].resize(numVariants);
    for (int variant = 0; variant < numVariants; ++ variant) {
      std::string filename = makeSpriteFilename(animationFilenameComponent, variant);
      ok = ok && LoadAnimation(variant, filename.c_str(), graphicsPath, cachePath, palettes, animationType);
    }
  }
  
  // Load the icon.
  iconTexture.Load(iconPath, GL_CLAMP, GL_LINEAR, GL_LINEAR);
  
  if (!ok) {
    return false;
  }
  
  maxCenterY = 0;
  const auto& animationVariants = animations[static_cast<int>(UnitAnimation::Idle)];
  for (usize variant = 0; variant < animationVariants.size(); ++ variant) {
    const auto& animation = animationVariants[variant];
    for (int frame = 0; frame < animation.sprite.NumFrames() / kNumFacingDirections; ++ frame) {
      maxCenterY = std::max(maxCenterY, animation.sprite.frame(frame).graphic.centerY);
    }
  }
  
  return true;
}

int ClientUnitType::GetHealthBarHeightAboveCenter() const {
  constexpr float kHealthBarOffset = 10;
  return maxCenterY + kHealthBarOffset;
}

bool ClientUnitType::LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes, UnitAnimation type) {
  std::vector<SpriteAndTextures>& animationVector = animations[static_cast<int>(type)];
  SpriteAndTextures& item = animationVector[index];
  
  return LoadSpriteAndTexture(
      (graphicsPath / filename).c_str(),
      (cachePath / filename).c_str(),
      GL_CLAMP,
      GL_NEAREST,
      GL_NEAREST,
      &item.sprite,
      &item.graphicTexture,
      &item.shadowTexture,
      palettes);
}


ClientUnit::ClientUnit(int playerIndex, UnitType type, const QPointF& mapCoord, double creationServerTime)
    : ClientObject(ObjectType::Unit, playerIndex, creationServerTime),
      type(type),
      mapCoord(mapCoord),
      direction(rand() % kNumFacingDirections),
      currentAnimation(UnitAnimation::Idle),
      currentAnimationVariant(0),
      lastAnimationStartTime(-1),
      movementSegment(-1, mapCoord, QPointF(0, 0), UnitAction::Idle) {}

QPointF ClientUnit::GetCenterProjectedCoord(Map* map) {
  return map->MapCoordToProjectedCoord(mapCoord);
}

QRectF ClientUnit::GetRectInProjectedCoords(Map* map, double elapsedSeconds, bool shadow, bool outline) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTextures& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  
  QPointF centerProjectedCoord = GetCenterProjectedCoord(map);
  
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  int frameIndex = direction * framesPerDirection + static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % framesPerDirection;
  
  const Sprite::Frame::Layer& layer = shadow ? sprite.frame(frameIndex).shadow : sprite.frame(frameIndex).graphic;
  bool isGraphic = !shadow && !outline;
  return QRectF(
      centerProjectedCoord.x() - layer.centerX + (isGraphic ? 1 : 0),
      centerProjectedCoord.y() - layer.centerY + (isGraphic ? 1 : 0),
      layer.imageWidth + (isGraphic ? -2 : 0),
      layer.imageHeight + (isGraphic ? -2 : 0));
}

void ClientUnit::Render(
    Map* map,
    const std::vector<QRgb>& playerColors,
    SpriteShader* spriteShader,
    GLuint pointBuffer,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    double serverTime,
    bool shadow,
    bool outline) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTextures& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  const Texture& texture = shadow ? animationSpriteAndTexture.shadowTexture : animationSpriteAndTexture.graphicTexture;
  
  QPointF centerProjectedCoord = GetCenterProjectedCoord(map);
  
  // Update the animation.
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  
  if (lastAnimationStartTime < 0) {
    // Initialize lastAnimationStartTime.
    lastAnimationStartTime = serverTime;
  }
  int frame;
  while (true) {
    frame = std::max(0, static_cast<int>(framesPerSecond * (serverTime - lastAnimationStartTime) + 0.5f));
    if (frame < framesPerDirection) {
      break;
    }
    
    // A new animation starts. Choose a random animation variant.
    lastAnimationStartTime = std::min(serverTime, lastAnimationStartTime + framesPerDirection / framesPerSecond);
    // TODO: The "currentAnimationVariant == 1" condition is special-case handling to make the scout unit look nicer. Check how this should be handled in general in other cases.
    if (currentAnimationVariant == 1) {
      currentAnimationVariant = 0;
    } else {
      currentAnimationVariant = rand() % unitType.GetAnimations(currentAnimation).size();
    }
  }
  int frameIndex = direction * framesPerDirection + frame;
  
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
}

void ClientUnit::SetCurrentAnimation(UnitAnimation animation, double serverTime) {
  if (currentAnimation == animation) {
    return;
  }
  
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  
  currentAnimation = animation;
  lastAnimationStartTime = serverTime;
  currentAnimationVariant = rand() % unitType.GetAnimations(currentAnimation).size();
}

void ClientUnit::UpdateGameState(double serverTime) {
  // Update the unit's movment according to the movement segment.
  if (movementSegment.action == UnitAction::Task) {
    mapCoord = movementSegment.startPoint;
  } else {
    mapCoord = movementSegment.startPoint + (serverTime - movementSegment.serverTime) * movementSegment.speed;
  }
  
  // Update facing direction.
  if (movementSegment.speed != QPointF(0, 0)) {
    // This angle goes from (-3) * M_PI / 4 to (+5) * M_PI / 4, with 0 being the right direction in the projected view.
    double angle = -1 * (atan2(movementSegment.speed.y(), movementSegment.speed.x()) - M_PI / 4);
    if (angle < 0) {
      angle += 2 * M_PI;
    }
    direction = std::max(0, std::min(kNumFacingDirections, static_cast<int>(kNumFacingDirections * angle / (2 * M_PI) + 0.5f)));
    if (direction == kNumFacingDirections) {
      direction = 0;
    }
  }
  
  if (movementSegment.action == UnitAction::Task) {
    SetCurrentAnimation(UnitAnimation::Task, serverTime);
  } else if (movementSegment.speed == QPointF(0, 0)) {
    // If the movement is zero, set the unit to the final position and delete the segment.
    SetCurrentAnimation(UnitAnimation::Idle, serverTime);
    
    mapCoord = movementSegment.startPoint;
  } else {
    // Use moving animation
    SetCurrentAnimation(UnitAnimation::Walk, serverTime);
  }
}
