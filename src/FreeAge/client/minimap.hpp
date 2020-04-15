// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>
#include <QRgb>

#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/shader_minimap.hpp"

class Map;

/// Handles the minimap creation and display.
class Minimap {
 public:
  ~Minimap();
  
  void Update(Map* map, const std::vector<QRgb>& playerColors, QOpenGLFunctions_3_2_Core* f);
  
  void Render(const QPointF& topLeft, float uiScale, const std::shared_ptr<MinimapShader>& shader, QOpenGLFunctions_3_2_Core* f);
  
  /// Converts a screen coordinate (e.g., the cursor position) to the corresponding map coordinate.
  /// Returns true if the conversion was successful, false if the screen coordinate is not within the minimap.
  /// The results in mapCoordX and mapCoordY are only valid if true is returned.
  bool ScreenToMapCoord(int screenX, int screenY, const QPointF& topLeft, float uiScale, Map* map, float* mapCoordX, float* mapCoordY);
  
 private:
  bool haveTexture = false;
  GLuint textureId;
  
  bool haveGeometryBuffersBeenInitialized = false;
  GLuint vertexBuffer;
  float oldVertexData[24];
};
