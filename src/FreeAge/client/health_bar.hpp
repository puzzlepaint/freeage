// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

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
