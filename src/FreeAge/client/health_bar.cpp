// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/health_bar.hpp"

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/client/shader_health_bar.hpp"

void RenderHealthBar(
    const QRectF& projectedCoordsRect,
    float objectCenterProjectedCoordY,
    float fillAmount,
    const QRgb& color,
    HealthBarShader* healthBarShader,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight,
    QOpenGLFunctions_3_2_Core* f) {
  ShaderProgram* program = healthBarShader->GetProgram();
  program->UseProgram(f);
  
  f->glUniform3f(healthBarShader->GetPlayerColorLocation(), qRed(color), qGreen(color), qBlue(color));
  f->glUniform1f(healthBarShader->GetFillAmountLocation(), fillAmount);
  f->glUniform2f(
      healthBarShader->GetSizeLocation(),
      zoom * 2.f * projectedCoordsRect.width() / static_cast<float>(widgetWidth),
      zoom * 2.f * projectedCoordsRect.height() / static_cast<float>(widgetHeight));
  
  constexpr float kOffScreenDepthBufferExtent = 1000;
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0,
      f);
  
  float* data = static_cast<float*>(f->glMapBufferRange(GL_ARRAY_BUFFER, 0, 3 * sizeof(float), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT));
  data[0] = projectedCoordsRect.x();
  data[1] = projectedCoordsRect.y();
  data[2] = 1.f - 2.f * (kOffScreenDepthBufferExtent + viewMatrix[0] * objectCenterProjectedCoordY + viewMatrix[2]) / (2.f * kOffScreenDepthBufferExtent + widgetHeight);
  f->glUnmapBuffer(GL_ARRAY_BUFFER);
  
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}
