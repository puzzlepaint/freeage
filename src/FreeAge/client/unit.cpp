// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/unit.hpp"

#include <cmath>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/client/mod_manager.hpp"

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

bool ClientUnitType::Load(UnitType type, const std::filesystem::path& graphicsSubPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes) {
  std::filesystem::path ingameUnitsSubPath = std::filesystem::path("widgetui") / "textures" / "ingame" / "units";
  
  // Later entries are used as fallbacks if the previous do not contain an animation type.
  // For example, for stone-miner-villagers, there is only a single sprite specific to stone mining,
  // the other mining sprites are (re)used from the gold miner, and for attacking we need the generic villager sprite.
  // So, these three base names are specified in this order here for this villager type.
  constexpr int kMaxNumBaseNames = 3;
  std::string spriteBaseName[3];
  std::filesystem::path iconSubPath;
  
  bool ok = true;
  switch (type) {
  case UnitType::FemaleVillager:
    spriteBaseName[0] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerBuilder:
    spriteBaseName[0] = "u_vil_female_builder";
    spriteBaseName[1] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerForager:
    spriteBaseName[0] = "u_vil_female_forager";
    spriteBaseName[1] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerLumberjack:
    spriteBaseName[0] = "u_vil_female_lumberjack";
    spriteBaseName[1] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerGoldMiner:
    spriteBaseName[0] = "u_vil_female_miner_gold";
    spriteBaseName[1] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::FemaleVillagerStoneMiner:
    spriteBaseName[0] = "u_vil_female_miner_stone";
    spriteBaseName[1] = "u_vil_female_miner_gold";
    spriteBaseName[2] = "u_vil_female_villager";
    iconSubPath = ingameUnitsSubPath / "016_50730.DDS";
    break;
  case UnitType::MaleVillager:
    spriteBaseName[0] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerBuilder:
    spriteBaseName[0] = "u_vil_male_builder";
    spriteBaseName[1] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerForager:
    spriteBaseName[0] = "u_vil_male_forager";
    spriteBaseName[1] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerLumberjack:
    spriteBaseName[0] = "u_vil_male_lumberjack";
    spriteBaseName[1] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerGoldMiner:
    spriteBaseName[0] = "u_vil_male_miner_gold";
    spriteBaseName[1] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::MaleVillagerStoneMiner:
    spriteBaseName[0] = "u_vil_male_miner_stone";
    spriteBaseName[1] = "u_vil_male_miner_gold";
    spriteBaseName[2] = "u_vil_male_villager";
    iconSubPath = ingameUnitsSubPath / "015_50730.DDS";
    break;
  case UnitType::Militia:
    spriteBaseName[0] = "u_inf_militia";
    iconSubPath = ingameUnitsSubPath / "008_50730.DDS";
    break;
  case UnitType::Scout:
    spriteBaseName[0] = "u_cav_scout";
    iconSubPath = ingameUnitsSubPath / "064_50730.DDS";
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
        if (std::filesystem::exists(GetModdedPath(graphicsSubPath / filename))) {
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
      ok = ok && LoadAnimation(variant, animationVariants[variant].c_str(), graphicsSubPath, cachePath, colorDilationShader, palettes, animationType);
      if (!ok) {
        return false;
      }
      
      // For extracting attack durations.
      // TODO: Remove this once we get those in a better way.
      if (animationType == UnitAnimation::Attack) {
        LOG(INFO) << "Attack animation " << animationVariants[variant] << " has " << (animations[static_cast<int>(animationType)][variant]->sprite.NumFrames() / kNumFacingDirections) << " frames per facing direction";
      }
    }
  }
  
  // Load the icon.
  iconTexture = TextureManager::Instance().GetOrLoad(GetModdedPath(iconSubPath), TextureManager::Loader::Mango, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
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

bool ClientUnitType::LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsSubPath, const std::filesystem::path& cachePath, ColorDilationShader* colorDilationShader, const Palettes& palettes, UnitAnimation type) {
  std::vector<SpriteAndTextures*>& animationVector = animations[static_cast<int>(type)];
  animationVector[index] = SpriteManager::Instance().GetOrLoad(
      GetModdedPath(graphicsSubPath / filename).string().c_str(),
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

QRectF ClientUnit::GetRectInProjectedCoords(Map* map, double serverTime, bool shadow, bool outline) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTextures& animationSpriteAndTexture = *unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  
  QPointF centerProjectedCoord = GetCenterProjectedCoord(map);
  
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  double animationTime = (idleBlockedStartTime > 0) ? idleBlockedStartTime : serverTime;
  int frameIndex = GetDirection(serverTime) * framesPerDirection + static_cast<int>(animationFramesPerSecond * animationTime + 0.5f) % framesPerDirection;
  
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
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  
  if (lastAnimationStartTime < 0) {
    // Initialize lastAnimationStartTime.
    lastAnimationStartTime = serverTime;
  }
  int frame;
  while (true) {
    double animationTime = (idleBlockedStartTime > 0) ? idleBlockedStartTime : serverTime;
    frame = std::max(0, static_cast<int>(animationFramesPerSecond * (animationTime - lastAnimationStartTime) + 0.5f));
    if (frame < framesPerDirection) {
      break;
    }
    
    // A new animation starts. Choose a random animation variant.
    lastAnimationStartTime = std::min(serverTime, lastAnimationStartTime + framesPerDirection / animationFramesPerSecond);
    // TODO: The "currentAnimationVariant == 1" condition is special-case handling to make the scout unit look nicer. Check how this should be handled in general in other cases.
    if (currentAnimationVariant == 1) {
      currentAnimationVariant = 0;
    } else {
      currentAnimationVariant = rand() % unitType.GetAnimations(currentAnimation).size();
    }
  }
  int frameIndex = GetDirection(serverTime) * framesPerDirection + frame;
  
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
  idleBlockedStartTime = -1;
  currentAnimationVariant = rand() % unitType.GetAnimations(currentAnimation).size();
}

Texture& ClientUnit::GetTexture(bool shadow) {
  const ClientUnitType& unitType = GetClientUnitType();
  SpriteAndTextures& animationSpriteAndTexture = *unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  return shadow ? animationSpriteAndTexture.shadowTexture : animationSpriteAndTexture.graphicTexture;
}

static int ComputeFacingDirection(const QPointF& movement) {
  // This angle goes from (-3) * M_PI / 4 to (+5) * M_PI / 4, with 0 being the right direction in the projected view.
  double angle = -1 * (atan2(movement.y(), movement.x()) - M_PI / 4);
  if (angle < 0) {
    angle += 2 * M_PI;
  }
  int direction = std::max(0, std::min(kNumFacingDirections, static_cast<int>(kNumFacingDirections * angle / (2 * M_PI) + 0.5f)));
  if (direction == kNumFacingDirections) {
    direction = 0;
  }
  return direction;
}

void ClientUnit::SetMovementSegment(double serverTime, const QPointF& startPoint, const QPointF& speed, UnitAction action, Map* map, Match* match) {
  // Check whether the unit moved differently than we expected.
  // In this case, for a short time, we change the facing direction to the direction
  // in which the unit actually moved.
  constexpr float kChangeDurationThreshold = 0.15f;
  constexpr float kOverrideDirectionDuration = 0.1f;
  
  UpdateMapCoord(serverTime, map, match);
  
  if (serverTime - movementSegment.serverTime < kChangeDurationThreshold) {
    overrideDirection = ComputeFacingDirection(startPoint - movementSegment.startPoint);
    overrideDirectionExpireTime = serverTime + kOverrideDirectionDuration;
  }
  
  // // NOTE: Old approach, triggers in cases where it should not, and yields wrong directions due to the "dead reckoning" on the client
  // 
  // constexpr float kUnexpectedMovementThreshold = 5e-3f;
  // constexpr float kOverrideDirectionDuration = 0.1f;
  // 
  // QPointF actualMoveDirection = startPoint - mapCoord;
  // if (actualMoveDirection.x() * actualMoveDirection.x() + actualMoveDirection.y() * actualMoveDirection.y() >= kUnexpectedMovementThreshold * kUnexpectedMovementThreshold) {
  //   overrideDirection = ComputeFacingDirection(actualMoveDirection);
  //   overrideDirectionExpireTime = serverTime + kOverrideDirectionDuration;
  // }
  
  // Store the received movement.
  movementSegment = MovementSegment(serverTime, startPoint, speed, action);
}

void ClientUnit::UpdateGameState(double serverTime, Map* map, Match* match) {
  // Update the unit's movement according to the movement segment.
  UpdateMapCoord(serverTime, map, match);
  
  // Update facing direction.
  if (movementSegment.speed != QPointF(0, 0)) {
    direction = ComputeFacingDirection(movementSegment.speed);
  }
  
  if (movementSegment.action == UnitAction::Task) {
    SetCurrentAnimation(UnitAnimation::Task, serverTime);
  } else if (movementSegment.action == UnitAction::Attack) {
    SetCurrentAnimation(UnitAnimation::Attack, serverTime);
  } else if (movementSegment.action == UnitAction::Idle) {
    if (currentAnimation != UnitAnimation::Idle) {
      if (movementSegment.speed == QPointF(0, 0)) {
        SetCurrentAnimation(UnitAnimation::Idle, serverTime);
      } else {
        // This means that the unit tries to move but cannot. Continue showing the
        // move animation for a short while before switching to the idle animation.
        // This avoids twitching.
        constexpr float kIdleBlockedAnimationDelay = 0.1f;
        
        if (idleBlockedStartTime < 0) {
          idleBlockedStartTime = serverTime;
        } else if (serverTime - idleBlockedStartTime >= kIdleBlockedAnimationDelay) {
          SetCurrentAnimation(UnitAnimation::Idle, serverTime);
        }
      }
    }
  } else if (currentAnimation != UnitAnimation::Walk) {
    SetCurrentAnimation(UnitAnimation::Walk, serverTime);
  }
  
  if (movementSegment.action != UnitAction::Idle) {
    idleBlockedStartTime = -1;
  }
}

void ClientUnit::UpdateMapCoord(double serverTime, Map* map, Match* match) {
  int oldTileX = static_cast<int>(mapCoord.x());
  int oldTileY = static_cast<int>(mapCoord.y());
  
  if (movementSegment.speed == QPointF(0, 0)) {
    mapCoord = movementSegment.startPoint;
  } else if (movementSegment.action == UnitAction::Idle ||
             movementSegment.action == UnitAction::Task ||
             movementSegment.action == UnitAction::Attack) {
    mapCoord = movementSegment.startPoint;
  } else {
    mapCoord = movementSegment.startPoint + (serverTime - movementSegment.serverTime) * movementSegment.speed;
  }
  
  int newTileX = static_cast<int>(mapCoord.x());
  int newTileY = static_cast<int>(mapCoord.y());
  
  if (match->GetPlayerIndex() == playerIndex &&
      (oldTileX != newTileX ||
       oldTileY != newTileY) && !IsGarrisoned()) {
    // TODO: This could be sped up by pre-computing only the *difference* that needs to be applied
    //       for a unit's line-of-sight when moving from one tile to an adjacent tile.
    //       Even without this pre-computation, iterating over the viewCount values only once
    //       should be faster (i.e., combine the two map->UpdateFieldOfView() calls below into
    //       one that takes both the old and the new coordinates).
    float lineOfSight = GetUnitLineOfSight(type);
    map->UpdateFieldOfView(oldTileX + 0.5f, oldTileY + 0.5f, lineOfSight, -1);
    map->UpdateFieldOfView(newTileX + 0.5f, newTileY + 0.5f, lineOfSight, 1);
  }
}

int ClientUnit::GetDirection(double serverTime) {
  return (serverTime >= overrideDirectionExpireTime) ? direction : overrideDirection;
}
