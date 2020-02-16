#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>
#include <QPointF>

#include "FreeAge/client_building.h"
#include "FreeAge/client_unit.h"
#include "FreeAge/shader_program.h"
#include "FreeAge/sprite.h"
#include "FreeAge/texture.h"

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
  
  /// Returns the occupancy state at the given tile.
  inline bool& occupiedAt(int tileX, int tileY) { return occupied[tileY * width + tileX]; }
  inline const bool& occupiedAt(int tileX, int tileY) const { return occupied[tileY * width + tileX]; }
  
  
  /// For testing, generates some kind of random map.
  void GenerateRandomMap();
  
  /// Adds a building to the map.
  void AddBuilding(int player, BuildingType type, int baseTileX, int baseTileY);
  
  /// Sets the given tile's elevation to the given value,
  /// while ensuring that the maximum slope of 1 is not exceeded
  /// (i.e., neighboring tiles may be modified as well).
  void PlaceElevation(int tileX, int tileY, int elevationValue);
  
  
  // TODO: Should this functionality be moved into its own class?
  void LoadRenderResources();
  void Render(
      float* viewMatrix,
      const std::vector<ClientUnitType>& unitTypes,
      const std::vector<Sprite>& buildingSprites,
      const std::vector<Texture>& buildingTextures,
      const std::vector<Texture>& buildingShadowTextures,
      const std::vector<QRgb>& playerColors,
      SpriteShader* shadowShader,
      SpriteShader* spriteShader,
      SpriteShader* outlineShader,
      GLuint spritePointBuffer,
      float zoom,
      int widgetWidth,
      int widgetHeight,
      double elapsedSeconds);
  
  
  inline QPointF GetTownCenterLocation(int player) { return townCenterCenters[player]; }
  
  inline int GetWidth() const { return width; }
  inline int GetHeight() const { return height; }
  
 private:
  /// The maximum possible elevation level (the lowest is zero).
  /// This may be higher than the maximum actually existing
  /// elevation level (but never lower).
  int maxElevation;
  
  /// 2D array storing the elevation level for each tile corner.
  /// The array size is thus: (width + 1) times (height + 1).
  /// An element (x, y) has index: [y * (width + 1) + x].
  int* elevation;
  
  /// 2D array storing whether each tile is occupied (for example,
  /// by a building). The array size is width times height.
  /// An element (x, y) has index: [y * width + x].
  bool* occupied;
  
  /// Width of the map in tiles.
  int width;
  
  /// Height of the map in tiles.
  int height;
  
  /// The next building ID that will be given to the next added building.
  int nextBuildingID = 0;
  
  /// Map of building ID -> ClientBuilding.
  std::unordered_map<int, ClientBuilding> buildings;
  
  /// The next unit ID that will be given to the next added unit.
  /// TODO: Unify building and unit IDs?
  int nextUnitID = 0;
  
  /// Map of unit ID -> ClientUnit.
  std::unordered_map<int, ClientUnit> units;
  
  /// Initial town center center locations of all players.
  std::vector<QPointF> townCenterCenters;
  
  // TODO: Should this functionality be moved into its own class?
  // --- Rendering attributes ---
  GLuint textureId;
  GLuint vertexBuffer;
  GLuint indexBuffer;
  std::shared_ptr<ShaderProgram> program;
  GLint program_u_texture_location;
  GLint program_u_viewMatrix_location;
};
