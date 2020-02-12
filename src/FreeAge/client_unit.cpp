#include "FreeAge/client_unit.h"

#include "FreeAge/logging.h"
#include "FreeAge/map.h"

bool ClientUnitType::Load(UnitType type, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes) {
  animations.resize(static_cast<int>(UnitAnimation::NumAnimationTypes));
  
  // TODO: Automatically check how many animations (A, B) for each type are available.
  
  bool ok = true;
  switch (type) {
  case UnitType::FemaleVillager:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(1);
    ok = ok && LoadAnimation(0, "u_vil_female_villager_idleA_x1.smx", graphicsPath, cachePath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::MaleVillager:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(1);
    ok = ok && LoadAnimation(0, "u_vil_male_villager_idleA_x1.smx", graphicsPath, cachePath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::Scout:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(2);
    ok = ok && LoadAnimation(0, "u_cav_scout_idleA_x1.smx", graphicsPath, cachePath, palettes, UnitAnimation::Idle);
    ok = ok && LoadAnimation(1, "u_cav_scout_idleB_x1.smx", graphicsPath, cachePath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::NumUnits:
    LOG(ERROR) << "Invalid unit type in ClientUnitType constructor: " << static_cast<int>(type);
    ok = false;
    break;
  }
  
  return ok;
}

bool ClientUnitType::LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, const Palettes& palettes, UnitAnimation type) {
  std::vector<SpriteAndTextures>& animationVector = animations[static_cast<int>(type)];
  SpriteAndTextures& item = animationVector[index];
  
  return LoadSpriteAndTexture(
      (graphicsPath / filename).c_str(),
      (cachePath / filename).c_str(),
      GL_CLAMP,
      GL_LINEAR,
      GL_LINEAR,
      &item.sprite,
      &item.graphicTexture,
      &item.shadowTexture,
      palettes);
}


ClientUnit::ClientUnit(UnitType type, const QPointF& mapCoord)
    : type(type),
      mapCoord(mapCoord),
      direction(rand() % kNumFacingDirections),
      currentAnimation(UnitAnimation::Idle),
      currentAnimationVariant(0),
      lastAnimationStartTime(-1) {}

QRectF ClientUnit::GetRectInProjectedCoords(Map* map, const std::vector<ClientUnitType>& unitTypes, double elapsedSeconds, bool shadow) {
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTextures& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(mapCoord);
  
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  int frameIndex = direction * framesPerDirection + static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % framesPerDirection;
  
  const Sprite::Frame::Layer& layer = shadow ? sprite.frame(frameIndex).shadow : sprite.frame(frameIndex).graphic;
  return QRectF(
      centerProjectedCoord.x() - layer.centerX,
      centerProjectedCoord.y() - layer.centerY,
      layer.imageWidth,
      layer.imageHeight);
}

void ClientUnit::Render(Map* map, const std::vector<ClientUnitType>& unitTypes, SpriteShader* spriteShader, GLuint pointBuffer, float zoom, int widgetWidth, int widgetHeight, double elapsedSeconds, bool shadow) {
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTextures& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  const Texture& texture = shadow ? animationSpriteAndTexture.shadowTexture : animationSpriteAndTexture.graphicTexture;
  
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(mapCoord);
  
  // Update the animation.
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  
  if (lastAnimationStartTime < 0) {
    // Initialize lastAnimationStartTime.
    lastAnimationStartTime = elapsedSeconds;
  }
  int frame;
  while (true) {
    frame = std::max(0, static_cast<int>(framesPerSecond * (elapsedSeconds - lastAnimationStartTime) + 0.5f));
    if (frame < framesPerDirection) {
      break;
    }
    
    // A new animation starts. Choose a random animation variant.
    lastAnimationStartTime = std::min(elapsedSeconds, lastAnimationStartTime + framesPerDirection / framesPerSecond);
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
      zoom,
      widgetWidth,
      widgetHeight,
      frameIndex,
      shadow);
}
