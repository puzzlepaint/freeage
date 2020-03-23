#pragma once

#include <QOpenGLFunctions_3_2_Core>
#include <QRectF>
#include <QRgb>

#include "FreeAge/client/opengl.hpp"

class HealthBarShader;

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
    QOpenGLFunctions_3_2_Core* f);
