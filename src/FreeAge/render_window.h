#pragma once

#include <chrono>
#include <memory>

#include <QOpenGLWidget>

#include "FreeAge/map.h"
#include "FreeAge/shader_sprite.h"
#include "FreeAge/sprite.h"
#include "FreeAge/texture.h"

typedef std::chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;

class RenderWindow : public QOpenGLWidget {
 Q_OBJECT
 public:
  RenderWindow(const Palettes& palettes, const std::filesystem::path& graphicsPath, QWidget* parent = nullptr);
  ~RenderWindow();
  
  void SetMap(Map* map);
  
  /// Scrolls the given map coordinates by the given amount in projected coordinates.
  void Scroll(float x, float y, QPointF* mapCoord);
  
  /// Computes the current scroll, taking into account the currently pressed keys.
  QPointF GetCurrentScroll(const TimePoint& atTime);
  
 protected:
  virtual void initializeGL() override;
  virtual void paintGL() override;
  virtual void resizeGL(int width, int height) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;
  virtual void wheelEvent(QWheelEvent* event) override;
  virtual void keyPressEvent(QKeyEvent* event) override;
  virtual void keyReleaseEvent(QKeyEvent* event) override;
  
  /// Current map scroll position in map coordinates.
  /// The "scroll" map coordinate is visible at the center of the screen.
  QPointF scroll;
  
  bool scrollRightPressed = false;
  TimePoint scrollRightPressTime;
  
  bool scrollLeftPressed = false;
  TimePoint scrollLeftPressTime;
  
  bool scrollUpPressed = false;
  TimePoint scrollUpPressTime;
  
  bool scrollDownPressed = false;
  TimePoint scrollDownPressTime;
  
  static constexpr const float scrollDistancePerSecond = 2000;  // TODO: Make configurable
  
  /// Map data.
  Map* map;
  
  /// Current zoom factor. The default zoom is one, two would make everything twice as big, etc.
  float zoom;
  
  /// Game start time.
  TimePoint gameStartTime;
  
  /// Cached widget size.
  int widgetWidth;
  int widgetHeight;
  
  // Shaders.
  std::shared_ptr<SpriteShader> spriteShader;
  
  // Resources.
  GLuint pointBuffer;
  
  std::vector<Sprite> buildingSprites;
  std::vector<Texture> buildingTextures;
  
  const Palettes& palettes;
  const std::filesystem::path& graphicsPath;
};
