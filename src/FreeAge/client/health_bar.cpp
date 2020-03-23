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
  
  float data[] = {
      static_cast<float>(projectedCoordsRect.x()),
      static_cast<float>(projectedCoordsRect.y()),
      static_cast<float>(1.f - 2.f * (kOffScreenDepthBufferExtent + viewMatrix[0] * objectCenterProjectedCoordY + viewMatrix[2]) / (2.f * kOffScreenDepthBufferExtent + widgetHeight))};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_STREAM_DRAW);
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}
