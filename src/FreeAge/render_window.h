#pragma once

#include <chrono>
#include <memory>

#include <QOpenGLWidget>

#include "FreeAge/shader_program.h"
#include "FreeAge/sprite.h"

class RenderWindow : public QOpenGLWidget {
 Q_OBJECT
 public:
  RenderWindow(QWidget* parent = nullptr);
  ~RenderWindow();
  
  void SetSprite(Sprite* sprite, const QImage& atlasImage);
  
 protected:
  void CreateConstantColorProgram();
  void LoadSprite();
  void DrawTestRect(int x, int y, int width, int height, int frameNumber);
  
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
  
  std::shared_ptr<ShaderProgram> constantColorProgram;
  GLint constantColorProgram_u_texture_location;
  GLint constantColorProgram_u_size_location;
  GLint constantColorProgram_u_tex_topleft_location;
  GLint constantColorProgram_u_tex_bottomright_location;
  
  Sprite* sprite;
  QImage atlasImage;
  GLuint textureId;
  
  // Game start time.
  std::chrono::steady_clock::time_point gameStartTime;
  
  // Cached widget size.
  int widgetWidth;
  int widgetHeight;
};
