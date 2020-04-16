// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <cmath>

#include <QPointF>

// ##############################################################################
// Since we use QPointF as a class for vectors, which lack some basic operations,
// we define some additional common functions for them here.
// ##############################################################################

/// Returns the squared length of the QPointF (interpreted as a vector starting from the origin, pointing to the point).
inline float SquaredLength(const QPointF& vector) {
  return vector.x() * vector.x() + vector.y() * vector.y();
}

/// Returns the length of the QPointF (interpreted as a vector starting from the origin, pointing to the point).
inline float Length(const QPointF& vector) {
  return sqrtf(SquaredLength(vector));
}

/// Returns the squared distance between the two points.
inline float SquaredDistance(const QPointF& a, const QPointF& b) {
  float dx = a.x() - b.x();
  float dy = a.y() - b.y();
  return dx * dx + dy * dy;
}

/// Returns the distance between the two points.
inline float Distance(const QPointF& a, const QPointF& b) {
  return sqrtf(SquaredDistance(a, b));
}

/// Rotates the input vector to the right by 90 degrees.
inline QPointF RightVector(const QPointF& inputVector) {
  return QPointF(-inputVector.y(), inputVector.x());
}

/// Computes the dot product of the two QPointF interpreted as vectors.
inline float Dot(const QPointF& a, const QPointF& b) {
  return a.x() * b.x() + a.y() * b.y();
}

/// Returns whether the two lines intersect, each given by a point on the line and the line direction.
/// If yes, the intersection point is returned as well.
/// Adapted from: https://stackoverflow.com/a/1968345/2676564
inline bool IntersectLines(const QPointF& a, const QPointF& aDir, const QPointF& b, const QPointF& bDir, QPointF* intersection) {
  float s1_x = aDir.x();
  float s1_y = aDir.y();
  float s2_x = bDir.x();
  float s2_y = bDir.y();
  
  float denominator = -s2_x * s1_y + s1_x * s2_y;
  if (denominator != 0) {
    float t = (s2_x * (a.y() - b.y()) - s2_y * (a.x() - b.x())) / denominator;
    intersection->setX(a.x() + (t * s1_x));
    intersection->setY(a.y() + (t * s1_y));
    return true;
  }
  
  return false;
}
