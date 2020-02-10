#pragma once

#include <chrono>
#include <memory>

#include <QOpenGLWidget>

#include "FreeAge/map.h"
#include "FreeAge/shader_program.h"
#include "FreeAge/sprite.h"

typedef std::chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;

class RenderWindow : public QOpenGLWidget {
 Q_OBJECT
 public:
  RenderWindow(QWidget* parent = nullptr);
  ~RenderWindow();
  
  void SetSprite(Sprite* sprite, const QImage& atlasImage);
  void SetMap(Map* map);
  
  /// Scrolls the given map coordinates by the given amount in projected coordinates.
  void Scroll(float x, float y, QPointF* mapCoord);
  
  /// Computes the current scroll, taking into account the currently pressed keys.
  QPointF GetCurrentScroll(const TimePoint& atTime);
  
 protected:
  void CreateConstantColorProgram();
  void LoadSprite();
  void DrawSprite(float x, float y, int width, int height, int frameNumber);
  
  virtual void initializeGL() override;
  virtual void paintGL() override;
  virtual void resizeGL(int width, int height) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;
  virtual void wheelEvent(QWheelEvent* event) override;
  virtual void keyPressEvent(QKeyEvent* event) override;
  virtual void keyReleaseEvent(QKeyEvent* event) override;
  
  GLuint pointBuffer;
  
  std::shared_ptr<ShaderProgram> spriteProgram;
  GLint spriteProgram_u_texture_location;
  GLint spriteProgram_u_viewMatrix_location;
  GLint spriteProgram_u_size_location;
  GLint spriteProgram_u_tex_topleft_location;
  GLint spriteProgram_u_tex_bottomright_location;
  
  Sprite* sprite;
  QImage atlasImage;
  GLuint textureId;
  
  Map* map;
  
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
  
  /// Current zoom factor. The default zoom is one, two would make everything twice as big, etc.
  float zoom;
  
  /// Game start time.
  TimePoint gameStartTime;
  
  /// Cached widget size.
  int widgetWidth;
  int widgetHeight;
};
