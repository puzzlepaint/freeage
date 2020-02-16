#include "FreeAge/health_bar.h"

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/free_age.h"
#include "FreeAge/shader_health_bar.h"

void RenderHealthBar(
    const QRectF& projectedCoordsRect,
    float objectCenterProjectedCoordY,
    float fillAmount,
    const QRgb& color,
    HealthBarShader* healthBarShader,
    GLuint pointBuffer,
    float* viewMatrix,
    float zoom,
    int widgetWidth,
    int widgetHeight) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  ShaderProgram* program = healthBarShader->GetProgram();
  program->UseProgram();
  
  program->SetUniform3f(healthBarShader->GetPlayerColorLocation(), qRed(color), qGreen(color), qBlue(color));
  program->SetUniform1f(healthBarShader->GetFillAmountLocation(), fillAmount);
  program->SetUniform2f(
      healthBarShader->GetSizeLocation(),
      zoom * 2.f * projectedCoordsRect.width() / static_cast<float>(widgetWidth),
      zoom * 2.f * projectedCoordsRect.height() / static_cast<float>(widgetHeight));
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  constexpr float kOffScreenDepthBufferExtent = 1000;
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0);
  
  float data[] = {
      static_cast<float>(projectedCoordsRect.x()),
      static_cast<float>(projectedCoordsRect.y()),
      static_cast<float>(1.f - 2.f * (kOffScreenDepthBufferExtent + viewMatrix[0] * objectCenterProjectedCoordY + viewMatrix[2]) / (2.f * kOffScreenDepthBufferExtent + widgetHeight))};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}
