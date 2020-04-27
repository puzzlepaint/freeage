// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/decal.hpp"

Decal::Decal(ClientUnit* unit, Map* map, double serverTime) {
  projectedCoord = map->MapCoordToProjectedCoord(unit->GetMapCoord());
  bool unitCarries =
      unit->GetCurrentAnimation() == UnitAnimation::CarryIdle ||
      unit->GetCurrentAnimation() == UnitAnimation::CarryWalk;
  type = unitCarries ? DecalType::UnitCarryDeath : DecalType::UnitDeath;
  unitType = unit->GetType();
  direction = unit->GetDirection();
  player = unit->GetPlayer();
  creationTime = serverTime;

  // If the unit type does not have a CarryDeath animation, use the standard Death animation instead.
  bool frameWasClamped;
  if (type == DecalType::UnitCarryDeath &&
      !GetCurrentSpriteAndFrame(serverTime, &currentSprite, &currentFrame, &frameWasClamped)) {
    type = DecalType::UnitDeath;
  }
  
  minTileX = std::max<int>(0, std::min<int>(map->GetWidth() - 1, unit->GetMapCoord().x()));
  minTileY = std::max<int>(0, std::min<int>(map->GetHeight() - 1, unit->GetMapCoord().y()));
  maxTileX = minTileX;
  maxTileY = minTileY;
}

Decal::Decal(ClientBuilding* building, Map* map, double serverTime) {
  projectedCoord = map->MapCoordToProjectedCoord(building->GetCenterMapCoord());
  type = DecalType::BuildingDestruction;
  buildingType = building->GetType();
  buildingOriginalFrameIndex = building->GetFrameIndex(serverTime);
  player = building->GetPlayer();
  creationTime = serverTime;
  
  const QPoint& baseTile = building->GetBaseTile();
  minTileX = baseTile.x();
  minTileY = baseTile.y();
  QSize size = GetBuildingSize(building->GetType());
  maxTileX = minTileX + size.width() - 1;
  maxTileY = minTileY + size.height() - 1;
}

bool Decal::Update(double serverTime) {
  bool frameWasClamped;
  if (!GetCurrentSpriteAndFrame(serverTime, &currentSprite, &currentFrame, &frameWasClamped)) {
    return false;
  }
  
  // For UnitDeath, UnitCarryDeath, and BuildingDestruction decals, check whether we need to switch
  // to the type that follows them (UnitDecay, UnitCarryDecay, and BuildingRubble).
  if (frameWasClamped && (type == DecalType::UnitDeath || type == DecalType::UnitCarryDeath || type == DecalType::BuildingDestruction)) {
    if (type == DecalType::UnitDeath) {
      type = DecalType::UnitDecay;
    } else if (type == DecalType::UnitCarryDeath) {
      type = DecalType::UnitCarryDecay;
    } else if (type == DecalType::BuildingDestruction) {
      type = DecalType::BuildingRubble;
    }
    creationTime = serverTime;
  }
  
  // TODO: We currently never expire decals. Consider doing that, potentially after a long time.
  return true;
}

QRectF Decal::GetRectInProjectedCoords(bool shadow, bool outline) {
  auto& currentFrameStruct = currentSprite->sprite.frame(currentFrame);
  const Sprite::Frame::Layer& layer = shadow ? currentFrameStruct.shadow : currentFrameStruct.graphic;
  bool isGraphic = !shadow && !outline;
  return QRectF(
      projectedCoord.x() - layer.centerX + (isGraphic ? 1 : 0),
      projectedCoord.y() - layer.centerY + (isGraphic ? 1 : 0),
      layer.imageWidth + (isGraphic ? -2 : 0),
      layer.imageHeight + (isGraphic ? -2 : 0));
}

void Decal::Render(QRgb outlineColor, SpriteShader* spriteShader, float* viewMatrix, float zoom, int widgetWidth, int widgetHeight, bool shadow, bool outline, Texture** texture) {
  *texture = shadow ? &currentSprite->shadowTexture : &currentSprite->graphicTexture;
  DrawSprite(
      currentSprite->sprite,
      **texture,
      spriteShader,
      projectedCoord,
      viewMatrix,
      zoom,
      widgetWidth,
      widgetHeight,
      currentFrame,
      shadow,
      outline,
      outlineColor,
      player->index,
      1.f);
}

int Decal::GetFPS() {
  if (type == DecalType::UnitDeath || type == DecalType::UnitCarryDeath) {
    return 30;
  } else if (type == DecalType::UnitDecay || type == DecalType::UnitCarryDecay) {
    return 2;  // TODO: What is the correct decay FPS?
  } else if (type == DecalType::BuildingDestruction) {
    return 30;
  } else if (type == DecalType::BuildingRubble) {
    return 2;  // TODO: What is the correct rubble FPS?
  }
  LOG(ERROR) << "Type not handled: " << static_cast<int>(type);
  return 1;
}

bool Decal::GetCurrentSpriteAndFrame(double serverTime, SpriteAndTextures** sprite, int* frame, bool* frameWasClamped) {
  if (type == DecalType::UnitDeath ||
      type == DecalType::UnitDecay ||
      type == DecalType::UnitCarryDeath ||
      type == DecalType::UnitCarryDecay) {
    const std::vector<SpriteAndTextures*>& animationVariants = GetClientUnitType(unitType).GetAnimations(
        (type == DecalType::UnitDeath) ? UnitAnimation::Death :
            ((type == DecalType::UnitDecay) ? UnitAnimation::Decay :
                ((type == DecalType::UnitCarryDeath) ? UnitAnimation::CarryDeath : UnitAnimation::CarryDecay)));
    if (animationVariants.empty()) {
      return false;
    }
    
    // TODO: We only use the first animation variant here. There probably do not exist multiple animation variants for this though, do they?
    *sprite = animationVariants[0];
    int framesPerDirection = (*sprite)->sprite.NumFrames() / kNumFacingDirections;
    int frameWithinDirection = static_cast<int>((serverTime - creationTime) * GetFPS());
    *frameWasClamped = frameWithinDirection > framesPerDirection - 1;
    *frame = direction * framesPerDirection +
             std::max<int>(0, std::min<int>(framesPerDirection - 1, frameWithinDirection));
    
    return true;
  } else {
    auto& clientBuildingType = GetClientBuildingType(buildingType);
    *sprite = clientBuildingType.GetSprites()[
        static_cast<int>((type == DecalType::BuildingDestruction) ? BuildingSprite::Destruction : BuildingSprite::Rubble)];
    if (!*sprite) {
      return false;
    }
    
    int numDecalSpriteFrames = (*sprite)->sprite.NumFrames();
    if (clientBuildingType.UsesRandomSpriteFrame()) {
      int numMainSpriteFrames = clientBuildingType.GetSprites()[static_cast<int>(BuildingSprite::Building)]->sprite.NumFrames();
      CHECK(numDecalSpriteFrames % numMainSpriteFrames == 0) << "numDecalSpriteFrames: " << numDecalSpriteFrames << ", numMainSpriteFrames: " << numMainSpriteFrames;
      int framesPerVariant = numDecalSpriteFrames / numMainSpriteFrames;
      int frameWithinVariant = static_cast<int>((serverTime - creationTime) * GetFPS());
      *frameWasClamped = frameWithinVariant > framesPerVariant - 1;
      *frame = buildingOriginalFrameIndex * framesPerVariant +
               std::max<int>(0, std::min<int>(framesPerVariant - 1, frameWithinVariant));
    } else {
      *frame = static_cast<int>((serverTime - creationTime) * GetFPS());
      *frameWasClamped = *frame > numDecalSpriteFrames - 1;
      *frame = std::max<int>(0, std::min<int>(numDecalSpriteFrames - 1, *frame));
    }
    
    return true;
  }
}
