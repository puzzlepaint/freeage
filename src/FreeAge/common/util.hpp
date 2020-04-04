#pragma once

#include <cmath>

#include <QPointF>

// Since we use QPointF as a class for vectors, we define some additional common functions for them here.

inline float SquaredLength(const QPointF& vector) {
  return vector.x() * vector.x() + vector.y() * vector.y();
}

inline float Length(const QPointF& vector) {
  return sqrtf(SquaredLength(vector));
}

inline float SquaredDistance(const QPointF& a, const QPointF& b) {
  float dx = a.x() - b.x();
  float dy = a.y() - b.y();
  return dx * dx + dy * dy;
}

inline float Distance(const QPointF& a, const QPointF& b) {
  return sqrtf(SquaredDistance(a, b));
}
