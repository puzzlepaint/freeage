#include "FreeAge/client/decal.hpp"

Decal::Decal(ClientUnit* unit, Map* map, double serverTime) {
  projectedCoord = map->MapCoordToProjectedCoord(unit->GetMapCoord());
  bool unitCarries =
      unit->GetCurrentAnimation() == UnitAnimation::CarryIdle ||
      unit->GetCurrentAnimation() == UnitAnimation::CarryWalk;
  type = unitCarries ? DecalType::UnitCarryDeath : DecalType::UnitDeath;
  unitType = unit->GetType();
  direction = unit->GetDirection();
  playerIndex = unit->GetPlayerIndex();
  creationTime = serverTime;
}

Decal::Decal(ClientBuilding* building, Map* map, double serverTime) {
  projectedCoord = map->MapCoordToProjectedCoord(building->GetCenterMapCoord());
  type = DecalType::BuildingDestruction;
  playerIndex = building->GetPlayerIndex();
  creationTime = serverTime;
}

bool Decal::Update(double serverTime) {
  if (!GetCurrentSpriteAndFrame(serverTime, &currentSprite, &currentFrame)) {
    return false;
  }
  
  // For UnitDeath, UnitCarryDeath, and BuildingDestruction decals, check whether we need to switch
  // to the type that follows them (UnitDecay, UnitCarryDecay, and BuildingRubble).
  if (type == DecalType::UnitDeath || type == DecalType::UnitCarryDeath || type == DecalType::BuildingDestruction) {
    int framesPerDirection = currentSprite->sprite.NumFrames() / kNumFacingDirections;
    int unclampedFrame = direction * framesPerDirection + static_cast<int>((serverTime - creationTime) * GetFPS());
    if (unclampedFrame > currentFrame) {
      if (type == DecalType::UnitDeath) {
        type = DecalType::UnitDecay;
      } else if (type == DecalType::UnitCarryDeath) {
        type = DecalType::UnitCarryDecay;
      } else if (type == DecalType::BuildingDestruction) {
        type = DecalType::BuildingRubble;
      }
      creationTime = serverTime;
    }
  }
  
  // TODO: We currently never expire decals. Consider doing that.
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

void Decal::Render(QRgb outlineColor, SpriteShader* spriteShader, GLuint pointBuffer, float* viewMatrix, float zoom, int widgetWidth, int widgetHeight, bool shadow, bool outline) {
  DrawSprite(
      currentSprite->sprite,
      shadow ? currentSprite->shadowTexture : currentSprite->graphicTexture,
      spriteShader,
      projectedCoord,
      pointBuffer,
      viewMatrix,
      zoom,
      widgetWidth,
      widgetHeight,
      currentFrame,
      shadow,
      outline,
      outlineColor,
      playerIndex);
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

bool Decal::GetCurrentSpriteAndFrame(double serverTime, SpriteAndTextures** sprite, int* frame) {
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
    *frame = direction * framesPerDirection + std::max<int>(0, std::min<int>(framesPerDirection - 1, static_cast<int>((serverTime - creationTime) * GetFPS())));
    
    return true;
  } else {
    LOG(ERROR) << "Not implemented yet";  // TODO
    return false;
  }
}
