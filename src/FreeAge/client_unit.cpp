#include "FreeAge/client_unit.h"

#include "FreeAge/logging.h"
#include "FreeAge/map.h"

bool ClientUnitType::Load(UnitType type, const std::filesystem::path& graphicsPath, const Palettes& palettes) {
  animations.resize(static_cast<int>(UnitAnimation::NumAnimationTypes));
  
  // TODO: Automatically check how many animations (A, B) for each type are available.
  
  bool ok = true;
  switch (type) {
  case UnitType::FemaleVillager:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(1);
    ok = ok && LoadAnimation(0, "u_vil_female_villager_idleA_x1.smx", graphicsPath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::MaleVillager:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(1);
    ok = ok && LoadAnimation(0, "u_vil_male_villager_idleA_x1.smx", graphicsPath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::Scout:
    animations[static_cast<int>(UnitAnimation::Idle)].resize(2);
    ok = ok && LoadAnimation(0, "u_cav_scout_idleA_x1.smx", graphicsPath, palettes, UnitAnimation::Idle);
    ok = ok && LoadAnimation(1, "u_cav_scout_idleB_x1.smx", graphicsPath, palettes, UnitAnimation::Idle);
    break;
  case UnitType::NumUnits:
    LOG(ERROR) << "Invalid unit type in ClientUnitType constructor: " << static_cast<int>(type);
    ok = false;
    break;
  }
  
  return ok;
}

bool ClientUnitType::LoadAnimation(int index, const char* filename, const std::filesystem::path& graphicsPath, const Palettes& palettes, UnitAnimation type) {
  std::vector<SpriteAndTexture>& animationVector = animations[static_cast<int>(type)];
  SpriteAndTexture& item = animationVector[index];
  
  return LoadSpriteAndTexture(
      (graphicsPath / filename).c_str(),
      GL_CLAMP,
      GL_LINEAR,
      GL_LINEAR,
      &item.sprite,
      &item.texture,
      palettes);
}


ClientUnit::ClientUnit(UnitType type, const QPointF& mapCoord)
    : type(type),
      mapCoord(mapCoord),
      direction(rand() % kNumFacingDirections),
      currentAnimation(UnitAnimation::Idle),
      currentAnimationVariant(0) {}

QRectF ClientUnit::GetRectInProjectedCoords(Map* map, const std::vector<ClientUnitType>& unitTypes, float elapsedSeconds) {
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTexture& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(mapCoord);
  
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  int frameIndex = direction * framesPerDirection + static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % framesPerDirection;
  
  const Sprite::Frame::Layer& layer = sprite.frame(frameIndex).graphic;
  return QRectF(
      centerProjectedCoord.x() - layer.centerX,
      centerProjectedCoord.y() - layer.centerY,
      layer.image.width(),
      layer.image.height());
}

void ClientUnit::Render(Map* map, const std::vector<ClientUnitType>& unitTypes, SpriteShader* spriteShader, GLuint pointBuffer, float zoom, int widgetWidth, int widgetHeight, float elapsedSeconds) {
  const ClientUnitType& unitType = unitTypes[static_cast<int>(type)];
  const SpriteAndTexture& animationSpriteAndTexture = unitType.GetAnimations(currentAnimation)[currentAnimationVariant];
  const Sprite& sprite = animationSpriteAndTexture.sprite;
  const Texture& texture = animationSpriteAndTexture.texture;
  
  QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(mapCoord);
  
  float framesPerSecond = 30.f;
  int framesPerDirection = sprite.NumFrames() / kNumFacingDirections;
  int frameIndex = direction * framesPerDirection + static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % framesPerDirection;
  
  DrawSprite(
      sprite,
      texture,
      spriteShader,
      centerProjectedCoord,
      pointBuffer,
      zoom,
      widgetWidth,
      widgetHeight,
      frameIndex);
}
