// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>
#include <QPointF>

#include "FreeAge/client/building.hpp"
#include "FreeAge/client/unit.hpp"
#include "FreeAge/client/shader_program.hpp"
#include "FreeAge/client/shader_terrain.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/texture.hpp"

class HealthBarShader;
class SpriteShader;

/// Stores the map (terrain type, elevation, ...).
///
/// There are three important coordinate systems:
/// - Map coordinate system: Represents coordinates on the ground. Relevant for pathing etc.
/// - Projected coordinate system: Coordinates for displaying (given default zoom). The CPU code passes these coordinates to the GPU.
/// - Screen coordinate system: Final coordinates on the screen computed in the shader. This is a zoomed and translated version of the projected coordinate system.
///
/// The origin of the map coordinate system is on the left,
/// +x goes to the bottom-right,
/// +y goes to the top-right.
/// Tile corners are at successive integer values.
///
///         (0 , 2)
///            X
///           / \
///   (0, 1) X   X (1, 2)
///         / \ / \
/// (0, 0) X   X   X (2, 2)
///         \ / \ /
///   (1, 0) X   Y (2, 1)
///           \ /
///            X
///         (2 , 0)
///
/// In default zoom, the height of one tile in projected coordinates is 48 pixels,
/// while the width of one tile in projected coordinates is 96 pixels.
class Map {
 public:
  /// Creates a new map with the given size in tiles.
  Map(int width, int height);
  
  /// This deletes OpenGL objects, so an OpenGL context must be current.
  ~Map();
  
  /// Computes the projected coordinates for a map corner.
  QPointF TileCornerToProjectedCoord(int cornerX, int cornerY) const;
  
  /// Computes the projected coordinates for an arbitrary map coordinate.
  /// Interpolation between corners is performed using bilinear interpolation.
  QPointF MapCoordToProjectedCoord(const QPointF& mapCoord, QPointF* jacobianColumn0 = nullptr, QPointF* jacobianColumn1 = nullptr) const;
  
  /// Attempts to determine the map coordinates for the given projected coordinates.
  /// If the projected coordinates are outside of the map, false is returned.
  /// In any case, mapCoord will be set to the closest map coordinate to the given projected coordinate that was found.
  bool ProjectedCoordToMapCoord(const QPointF& projectedCoord, QPointF* mapCoord) const;
  
  /// Returns the elevation at the given tile corner.
  inline int& elevationAt(int cornerX, int cornerY) { return elevation[cornerY * (width + 1) + cornerX]; }
  inline const int& elevationAt(int cornerX, int cornerY) const { return elevation[cornerY * (width + 1) + cornerX]; }
  
  /// Returns the view count at the given tile.
  /// After you make changes, you must call ViewCountChanged().
  inline int& viewCountAt(int tileX, int tileY) { return viewCount[tileY * width + tileX]; }
  inline const int& viewCountAt(int tileX, int tileY) const { return viewCount[tileY * width + tileX]; }
  inline void ViewCountChanged(int minX, int minY, int maxX, int maxY) {
    viewCountChangeMinX = std::min(viewCountChangeMinX, minX);
    viewCountChangeMinY = std::min(viewCountChangeMinY, minY);
    viewCountChangeMaxX = std::max(viewCountChangeMaxX, maxX);
    viewCountChangeMaxY = std::max(viewCountChangeMaxY, maxY);
  }
  
  /// Writes the field-of-view of a unit or building into the viewCount.
  /// If change is 1, adds a view count, if it is -1, removes one.
  void UpdateFieldOfView(float centerMapCoordX, float centerMapCoordY, float radius, int change);
  
  
  // TODO: Should this functionality be moved into its own class?
  inline void SetNeedsRenderResourcesUpdate(bool needsUpdate) { needsRenderResourcesUpdate = needsUpdate; }
  void Render(float* viewMatrix, const std::filesystem::path& graphicsSubPath, QOpenGLFunctions_3_2_Core* f);
  void UnloadRenderResources();
  
  
  inline std::unordered_map<u32, ClientObject*>& GetObjects() { return objects; }
  inline const std::unordered_map<u32, ClientObject*>& GetObjects() const { return objects; }
  
  void AddObject(u32 objectId, ClientObject* object);
  
  inline int GetWidth() const { return width; }
  inline int GetHeight() const { return height; }
  
  inline int GetMaxElevation() const { return maxElevation; }
  
 private:
  void UpdateRenderResources(const std::filesystem::path& graphicsSubPath, QOpenGLFunctions_3_2_Core* f);
  void UpdateViewCountTexture(QOpenGLFunctions_3_2_Core* f);
  
  /// The maximum possible elevation level (the lowest is zero).
  /// This may be higher than the maximum actually existing
  /// elevation level (but never lower).
  int maxElevation;
  
  /// 2D array storing the elevation level for each tile corner.
  /// The array size is thus: (width + 1) times (height + 1).
  /// An element (x, y) has index: [y * (width + 1) + x].
  /// The special value of -1 means that the elevation at a corner is unknown
  /// (since it has not been uncovered yet).
  int* elevation;
  
  /// Width of the map in tiles.
  int width;
  
  /// Height of the map in tiles.
  int height;
  
  /// Map of object ID -> ClientObject.
  std::unordered_map<u32, ClientObject*> objects;
  
  /// Stores how many units or buildings view each map tile.
  /// As a special case, map tiles that have not been uncovered yet have the value -1.
  /// The array size is thus: width times height.
  /// An element (x, y) has index: [y * width + x].
  int* viewCount;
  
  /// The area where the view counts changed since the last rendering call
  /// (and thus must be updated before the next rendering call).
  /// If set to an invalid area, no update has been done.
  int viewCountChangeMinX;
  int viewCountChangeMinY;
  int viewCountChangeMaxX;
  int viewCountChangeMaxY;
  
  bool haveViewTexture = false;
  GLuint viewTextureId;
  
  // TODO: Should this functionality be moved into its own class?
  // --- Rendering attributes ---
  bool needsRenderResourcesUpdate = true;
  
  bool hasTextureBeenLoaded = false;
  GLuint textureId;
  bool haveGeometryBuffersBeenInitialized = false;
  GLuint vertexBuffer;
  GLuint indexBuffer;
  std::shared_ptr<TerrainShader> terrainShader;
};
