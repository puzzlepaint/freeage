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
  
 private:
  bool haveTexture = false;
  GLuint textureId;
  
  bool haveGeometryBuffersBeenInitialized = false;
  GLuint vertexBuffer;
  float oldVertexData[24];
};
