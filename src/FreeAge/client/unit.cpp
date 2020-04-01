#include "FreeAge/client/unit.hpp"

#include <cmath>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"

ClientUnitType::~ClientUnitType() {
  for (usize i = 0; i < animations.size(); ++ i) {
    for (usize k = 0; k < animations[i].size(); ++ k) {
      SpriteManager::Instance().Dereference(animations[i][k]);
    }
  }
  
  if (iconTexture) {
    TextureManager::Instance().Dereference(iconTexture);
  }
}

bool ClientUnitType::Load(UnitType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes) {
  std::filesystem::path ingameUnitsPath =
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame" / "units";
  
  // Later entries are used as fallbacks if the previous do not contain an animation type.
  // For example, for stone-miner-villagers, there is only a single sprite specific to stone mining,
  // the other mining sprites are (re)used from the gold miner, and for attacking we need the generic villager sprite.
  // So, these three base names are specified in this order here for this villager type.
  constexpr int kMaxNumBaseNames = 3;
  std::string spriteBaseName[3];
  std::string iconPath;
  
  bool ok = true;
  switch (type) {
  case UnitType::FemaleVillager:
    spriteBaseName[0] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::FemaleVillagerBuilder:
    spriteBaseName[0] = "u_vil_female_builder";
    spriteBaseName[1] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::FemaleVillagerForager:
    spriteBaseName[0] = "u_vil_female_forager";
    spriteBaseName[1] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::FemaleVillagerLumberjack:
    spriteBaseName[0] = "u_vil_female_lumberjack";
    spriteBaseName[1] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::FemaleVillagerGoldMiner:
    spriteBaseName[0] = "u_vil_female_miner_gold";
    spriteBaseName[1] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::FemaleVillagerStoneMiner:
    spriteBaseName[0] = "u_vil_female_miner_stone";
    spriteBaseName[1] = "u_vil_female_miner_gold";
    spriteBaseName[2] = "u_vil_female_villager";
    iconPath = (ingameUnitsPath / "016_50730.DDS").string();
    break;
  case UnitType::MaleVillager:
    spriteBaseName[0] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::MaleVillagerBuilder:
    spriteBaseName[0] = "u_vil_male_builder";
    spriteBaseName[1] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::MaleVillagerForager:
    spriteBaseName[0] = "u_vil_male_forager";
    spriteBaseName[1] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::MaleVillagerLumberjack:
    spriteBaseName[0] = "u_vil_male_lumberjack";
    spriteBaseName[1] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::MaleVillagerGoldMiner:
    spriteBaseName[0] = "u_vil_male_miner_gold";
    spriteBaseName[1] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::MaleVillagerStoneMiner:
    spriteBaseName[0] = "u_vil_male_miner_stone";
    spriteBaseName[1] = "u_vil_male_miner_gold";
    spriteBaseName[2] = "u_vil_male_villager";
    iconPath = (ingameUnitsPath / "015_50730.DDS").string();
    break;
  case UnitType::Militia:
    spriteBaseName[0] = "u_inf_militia";
    iconPath = (ingameUnitsPath / "008_50730.DDS").string();
    break;
  case UnitType::Scout:
    spriteBaseName[0] = "u_cav_scout";
    iconPath = (ingameUnitsPath / "064_50730.DDS").string();
    break;
  case UnitType::NumUnits:
    LOG(ERROR) << "Invalid unit type in ClientUnitType constructor: " << static_cast<int>(type);
    ok = false;
    break;
  }
  
  auto makeSpriteFilename = [&](const std::string& baseName, const std::string& animationFilename, int variant) {
    return baseName + "_" + animationFilename + static_cast<char>('A' + variant) + "_x1.smx";
  };
  
  animations.resize(static_cast<int>(UnitAnimation::NumAnimationTypes));
  for (int animationTypeInt = 0; animationTypeInt < static_cast<int>(UnitAnimation::NumAnimationTypes); ++ animationTypeInt) {
    UnitAnimation animationType = static_cast<UnitAnimation>(animationTypeInt);
    
    std::string animationFilenameComponent;
    switch (animationType) {
    case UnitAnimation::Idle: animationFilenameComponent = "idle"; break;
    case UnitAnimation::CarryIdle: animationFilenameComponent = "carryidle"; break;
    case UnitAnimation::Walk: animationFilenameComponent = "walk"; break;
    case UnitAnimation::CarryWalk: animationFilenameComponent = "carrywalk"; break;
    case UnitAnimation::Task: animationFilenameComponent = "task"; break;
    case UnitAnimation::Attack: animationFilenameComponent = "attack"; break;
    case UnitAnimation::Death: animationFilenameComponent = "death"; break;
    case UnitAnimation::CarryDeath: animationFilenameComponent = "carrydeath"; break;
    case UnitAnimation::Decay: animationFilenameComponent = "decay"; break;
    case UnitAnimation::CarryDecay: animationFilenameComponent = "carrydecay"; break;
    case UnitAnimation::NumAnimationTypes: LOG(ERROR) << "Invalid animation type.";
    }
    
    // Determine the number of animation variants.
    std::vector<std::string> animationVariants;
    for (int variant = 0; variant < 99; ++ variant) {
      bool fileExists = false;
      for (int fallbackNumber = 0; fallbackNumber < kMaxNumBaseNames; ++ fallbackNumber) {
        std::string filename = makeSpriteFilename(spriteBaseName[fallbackNumber], animationFilenameComponent, variant);
        if (std::filesystem::exists(graphicsPath / filename)) {
          animationVariants.push_back(filename);
          fileExists = true;
          break;
        }
      }
      
      if (!fileExists) {
        break;
      }
    }
    
    // Load each variant.
    animations[animationTypeInt].resize(animationVariants.size());
    for (usize variant = 0; variant < animationVariants.size(); ++ variant) {
      ok = ok && LoadAnimation(variant, animationVariants[variant].c_str(), graphicsPath, cachePath, colorDilationShader, palettes, animationType);
      
      // For extracting attack durations.
      // TODO: Remove this once we get those in a better way.
      if (animationType == UnitAnimation::Attack) {
        LOG(INFO) << "Attack animation " << animationVariants[variant] << " has " << (animations[static_cast<int>(animationType)][variant]->sprite.NumFrames() / kNumFacingDirections) << " frames per facing direction";
      }
    }
  }
  
  // Load the icon.
  iconTexture = TextureManager::Instance().GetOrLoad(iconPath, TextureManager::Loader::Mango, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  if (!ok) {
    return false;
  }
  
  maxCenterY = 0;
  const auto& animationVariants = animations[static_cast<int>(UnitAnimation::Idle)];
  for (usize variant = 0; variant < animationVariants.size(); ++ variant) {
    const auto& animation = animationVariants[variant];
    for (int frame = 0; frame < animation->sprite.NumFrames() / kNumFacingDirections; ++ frame) {
      maxCenterY = std::max(maxCenterY, animation->sprite.frame(frame).graphic.centerY);
    }
  }
  
  return true;
}

int ClientUnitType::GetHealthBarHeightAboveCenter() const {
  constexpr float kHealthBarOffset = 10;
  return maxCenterY + kHealthBarOffset;
}

bool ClientUnitType::LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes, UnitAnimation type) {
  std::vector<SpriteAndTextures*>& animationVector = animations[static_cast<int>(type)];
  animationVector[index] = SpriteManager::Instance().GetOrLoad(
      (graphicsPath / filename).string().c_str(),
      (cachePath / filename).string().c_str(),
      colorDilationShader,
      palettes);
  return animationVector[index] != nullptr;
}


ClientUnit::ClientUnit(int playerIndex, UnitType type, const QPointF& mapCoord, u32 hp)
    : ClientObject(ObjectType::Unit, playerIndex, hp),
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
  const SpriteAndTextures& animationSpriteAndTexture = *unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
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
    QRgb outlineOrModulationColor,
    SpriteShader* spriteShader,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    double serverTime,
    bool shadow,
    bool outline) {
  const ClientUnitType& unitType = GetClientUnitType();
  SpriteAndTextures& animationSpriteAndTexture = *unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  Texture& texture = shadow ? animationSpriteAndTexture.shadowTexture : animationSpriteAndTexture.graphicTexture;
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  
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
      viewMatrix,
      zoom,
      widgetWidth,
      widgetHeight,
      frameIndex,
      shadow,
      outline,
      outlineOrModulationColor,
      playerIndex,
      1.f);
}

void ClientUnit::SetCurrentAnimation(UnitAnimation animation, double serverTime) {
  const ClientUnitType& unitType = ClientUnitType::GetUnitTypes()[static_cast<int>(type)];
  
  // If this unit is a villager carrying the resource type corresponding to the villager type,
  // turn the idle/walk/death/decay animations into the corresponding "carry" variants if they exist.
  if (IsVillager(type) && carriedResourceAmount > 0 && GetResourceTypeOfVillagerType(type) == carriedResourceType) {
    if (animation == UnitAnimation::Idle &&
        !unitType.GetAnimations(UnitAnimation::CarryIdle).empty()) {
      animation = UnitAnimation::CarryIdle;
    } else if (animation == UnitAnimation::Walk &&
               !unitType.GetAnimations(UnitAnimation::CarryWalk).empty()) {
      animation = UnitAnimation::CarryWalk;
    }
  }
  
  if (currentAnimation == animation) {
    return;
  }
  
  currentAnimation = animation;
  lastAnimationStartTime = serverTime;
  currentAnimationVariant = rand() % unitType.GetAnimations(currentAnimation).size();
}

Texture& ClientUnit::GetTexture(bool shadow) {
  const ClientUnitType& unitType = GetClientUnitType();
  SpriteAndTextures& animationSpriteAndTexture = *unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  return shadow ? animationSpriteAndTexture.shadowTexture : animationSpriteAndTexture.graphicTexture;
}

void ClientUnit::UpdateGameState(double serverTime) {
  // Update the unit's movment according to the movement segment.
  if (movementSegment.action == UnitAction::Task ||
      movementSegment.action == UnitAction::Attack) {
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
  } else if (movementSegment.action == UnitAction::Attack) {
    SetCurrentAnimation(UnitAnimation::Attack, serverTime);
  } else if (movementSegment.speed == QPointF(0, 0)) {
    // If the movement is zero, set the unit to the final position and delete the segment.
    SetCurrentAnimation(UnitAnimation::Idle, serverTime);
    
    mapCoord = movementSegment.startPoint;
  } else {
    // Use moving animation
    SetCurrentAnimation(UnitAnimation::Walk, serverTime);
  }
}
