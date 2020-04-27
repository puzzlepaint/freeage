// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include "FreeAge/client/building.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/unit.hpp"

enum class DecalType {
  UnitDeath = 0,
  UnitDecay,
  UnitCarryDeath,
  UnitCarryDecay,
  BuildingDestruction,
  BuildingRubble
};

/// Displays (potentially animated) graphics that do not interact with other game objects.
/// This is used for unit death and building destruction animations,
/// as well as for unit decay and building rubble sprites.
class Decal {
 public:
  /// Creates a unit-death decal for the given unit.
  Decal(ClientUnit* unit, Map* map, double serverTime);
  
  /// Creates a building-destruction decal for the given building.
  Decal(ClientBuilding* building, Map* map, double serverTime);
  
  /// Updates the decal, must be called before each frame.
  /// Returns false if the decal has expired and should be removed.
  bool Update(double serverTime);
  
  QRectF GetRectInProjectedCoords(
      bool shadow,
      bool outline);
  
  void Render(
      QRgb outlineColor,
      SpriteShader* spriteShader,
      float* viewMatrix,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      bool shadow,
      bool outline,
      Texture** texture);
  
  inline bool MayOccludeSprites() const {
    return type == DecalType::UnitDeath || type == DecalType::BuildingDestruction;
  }
  
  inline u8 GetPlayerIndex() const { return player->index; }
  
  inline int GetMinTileX() const { return minTileX; }
  inline int GetMinTileY() const { return minTileY; }
  inline int GetMaxTileX() const { return maxTileX; }
  inline int GetMaxTileY() const { return maxTileY; }
  
 private:
  int GetFPS();
  bool GetCurrentSpriteAndFrame(double serverTime, SpriteAndTextures** sprite, int* frame, bool* frameWasClamped);
  
  
  /// Projected coordinate of the sprite center.
  /// Note that we do not store the map coordinate since decals never move on the map.
  QPointF projectedCoord;
  
  /// Extent of the decal on the map, in map tiles.
  /// This is used to determine the view state of the decal (visible / in fog of war) for correct rendering.
  int minTileX;
  int minTileY;
  int maxTileX;
  int maxTileY;
  
  /// Type of the decal.
  DecalType type;
  
  /// For UnitDeath / UnitDecay decals: type of the unit.
  UnitType unitType;
  
  /// For building-created decals: type of the building.
  BuildingType buildingType;
  
  /// For UnitDeath / UnitDecay? (TODO?) decals: facing direction of the unit.
  u8 direction;
  
  /// Index of the player which this decal belonged to when it was still an object
  const Player* player;
  
  /// For buildings that use a fixed frame index, this stores the frame index that the
  /// building that this decal was created from used. This is necessary to know the
  /// correct one of the destruction animations (in case there is a separate one for each
  /// frame of the building sprite).
  u16 buildingOriginalFrameIndex;
  
  /// Creation time of the decal (with its current type).
  /// Decals may decay after some time (rubble / decay),
  /// or play an animation over time (unit death / building destruction).
  double creationTime;
  
  
  /// Cached current sprite computed by GetCurrentSpriteAndFrame() in the last call to Update().
  SpriteAndTextures* currentSprite;
  /// Cached current frame index computed by GetCurrentSpriteAndFrame() in the last call to Update().
  int currentFrame;
};
