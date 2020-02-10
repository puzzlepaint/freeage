#include "FreeAge/render_window.h"

#include <math.h>

#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QTimer>

#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"

RenderWindow::RenderWindow(QWidget* parent)
    : QOpenGLWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  
  // Receive mouse move events even if no mouse button is pressed
  setMouseTracking(true);
  
  // This may be faster than keeping partial updates possible
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  
  // Do continuous rendering via a timer
  float framesPerSecondCap = 120;
  QTimer* timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, QOverload<>::of(&RenderWindow::update));
  // TODO: This is rounded to milliseconds, thus the FPS cap will be approximate
  timer->start(1000.f / framesPerSecondCap + 0.5f);
  
  // Initialize the view settings
  scroll = QPointF(0, 0);
  zoom = 1;
  
  
  sprite = nullptr;
  map = nullptr;
}

RenderWindow::~RenderWindow() {
  makeCurrent();
  // Destroy OpenGL resources here
  spriteProgram.reset();
  if (map) {
    delete map;
  }
  doneCurrent();
}

void RenderWindow::SetSprite(Sprite* sprite, const QImage& atlasImage) {
  this->sprite = sprite;
  this->atlasImage = atlasImage;
}

void RenderWindow::SetMap(Map* map) {
  this->map = map;
}

void RenderWindow::Scroll(float x, float y, QPointF* mapCoord) {
  QPointF projectedCoord = map->MapCoordToProjectedCoord(*mapCoord);
  projectedCoord += QPointF(x, y);
  map->ProjectedCoordToMapCoord(projectedCoord, mapCoord);
}

QPointF RenderWindow::GetCurrentScroll(const TimePoint& atTime) {
  QPointF projectedCoord = map->MapCoordToProjectedCoord(scroll);
  if (scrollRightPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollRightPressTime).count();
    projectedCoord += QPointF(scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollLeftPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollLeftPressTime).count();
    projectedCoord += QPointF(-scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollDownPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollDownPressTime).count();
    projectedCoord += QPointF(0, scrollDistancePerSecond / zoom * seconds);
  }
  if (scrollUpPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollUpPressTime).count();
    projectedCoord += QPointF(0, -scrollDistancePerSecond / zoom * seconds);
  }
  
  QPointF result;
  map->ProjectedCoordToMapCoord(projectedCoord, &result);
  return result;
}

void RenderWindow::CreateConstantColorProgram() {
  spriteProgram.reset(new ShaderProgram());
  
  CHECK(spriteProgram->AttachShader(
      "#version 330 core\n"
      "in vec3 in_position;\n"
      "uniform mat2 u_viewMatrix;\n"
      "void main() {\n"
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], in_position.z, 1);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader));
  
  CHECK(spriteProgram->AttachShader(
      "#version 330 core\n"
      "#extension GL_EXT_geometry_shader : enable\n"
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "\n"
      "uniform vec2 u_size;\n"
      "uniform vec2 u_tex_topleft;\n"
      "uniform vec2 u_tex_bottomright;\n"
      "\n"
      "out vec2 texcoord;\n"
      "\n"
      "void main() {\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_topleft.x, u_tex_topleft.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_bottomright.x, u_tex_topleft.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_topleft.x, u_tex_bottomright.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_bottomright.x, u_tex_bottomright.y);\n"
      "  EmitVertex();\n"
      "  \n"
      "  EndPrimitive();\n"
      "}\n",
      ShaderProgram::ShaderType::kGeometryShader));
  
  CHECK(spriteProgram->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "\n"
      "void main() {\n"
      "  out_color = texture(u_texture, texcoord.xy);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader));
  
  CHECK(spriteProgram->LinkProgram());
  
  spriteProgram->UseProgram();
  
  spriteProgram_u_texture_location =
      spriteProgram->GetUniformLocationOrAbort("u_texture");
  spriteProgram_u_viewMatrix_location =
      spriteProgram->GetUniformLocationOrAbort("u_viewMatrix");
  spriteProgram_u_size_location =
      spriteProgram->GetUniformLocationOrAbort("u_size");
  spriteProgram_u_tex_topleft_location =
      spriteProgram->GetUniformLocationOrAbort("u_tex_topleft");
  spriteProgram_u_tex_bottomright_location =
      spriteProgram->GetUniformLocationOrAbort("u_tex_bottomright");
}

void RenderWindow::LoadSprite() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // TODO
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // TODO
  
  // TODO: Use this for 8-bit-per-pixel data
  // // OpenGL by default tries to read data in multiples of 4, if our data is
  // // only RGB or BGR and the width is not divible by 4 then we need to alert
  // // opengl
  // if((img->width % 4) != 0 && 
  //  (img->format == GL_RGB || 
  //   img->format == GL_BGR))
  // {
  //   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // }
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      atlasImage.width(), atlasImage.height(),
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      atlasImage.scanLine(0));
  
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::DrawSprite(float x, float y, int width, int height, int frameNumber) {
  const Sprite::Frame::Layer& layer = sprite->frame(frameNumber).graphic;
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  spriteProgram->UseProgram();
  spriteProgram->SetUniform1i(spriteProgram_u_texture_location, 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  spriteProgram->SetUniform2f(spriteProgram_u_size_location, zoom * 2.f * width / static_cast<float>(widgetWidth), zoom * 2.f * height / static_cast<float>(widgetHeight));
  float texLeftX = layer.atlasX / (1.f * atlasImage.width());
  float texTopY = layer.atlasY / (1.f * atlasImage.height());
  float texRightX = (layer.atlasX + layer.image.width()) / (1.f * atlasImage.width());
  float texBottomY = (layer.atlasY + layer.image.height()) / (1.f * atlasImage.height());
  if (layer.rotated) {
    // TODO? Is this worth implementing? It will complicate the shader a little.
  }
  spriteProgram->SetUniform2f(spriteProgram_u_tex_topleft_location, texLeftX, texTopY);
  spriteProgram->SetUniform2f(spriteProgram_u_tex_bottomright_location, texRightX, texBottomY);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  float data[] = {x, y, 0};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  spriteProgram->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0);
  
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::initializeGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Create a vertex array object (VAO).
  // TODO: Handle this properly.
  GLuint vao;
  f->glGenVertexArrays(1, &vao);
  f->glBindVertexArray(vao);
  
  CreateConstantColorProgram();
  CHECK_OPENGL_NO_ERROR();
  
  if (map) {
    map->LoadRenderResources();
  }
  
  if (sprite) {
    LoadSprite();
    
    f->glGenBuffers(1, &pointBuffer);
    f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
    int elementSizeInBytes = 3 * sizeof(float);
    float data[] = {
        0.f, 0.f, 0};
    f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
    CHECK_OPENGL_NO_ERROR();
  }
  
  // Remember the game start time.
  gameStartTime = Clock::now();
}

void RenderWindow::paintGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Get the time for which to render the game state.
  // TODO: Predict the time at which the rendered frame will be displayed rather than taking the current time.
  TimePoint now = Clock::now();
  double elapsedSeconds = std::chrono::duration<double>(now - gameStartTime).count();
  
  // Update scrolling state
  scroll = GetCurrentScroll(now);
  if (scrollRightPressed) { scrollRightPressTime = now; }
  if (scrollLeftPressed) { scrollLeftPressTime = now; }
  if (scrollUpPressed) { scrollUpPressTime = now; }
  if (scrollDownPressed) { scrollDownPressTime = now; }
  
  // Compute the view (projected-to-OpenGL) transformation.
  // Projected coordinates: arbitrary origin, +x goes right, +y goes down, scale is the default scale.
  // OpenGL normalized device coordinates: top-left widget corner is (-1, 1), bottom-right widget corner is (1, -1).
  // The transformation is stored as a matrix but applied as follows:
  // opengl_x = viewMatrix[0] * projected_x + viewMatrix[2];
  // opengl_y = viewMatrix[1] * projected_y + viewMatrix[3];
  QPointF projectedCoordAtScreenCenter = map->MapCoordToProjectedCoord(scroll);
  float scalingX = zoom * 2.f / widgetWidth;
  float scalingY = zoom * -2.f / widgetHeight;
  
  float viewMatrix[4];  // column-major
  viewMatrix[0] = scalingX;
  viewMatrix[1] = scalingY;
  viewMatrix[2] = -scalingX * projectedCoordAtScreenCenter.x();
  viewMatrix[3] = -scalingY * projectedCoordAtScreenCenter.y();
  
  // Apply the view transformation to all shaders.
  // TODO: Use a uniform buffer object for that.
  spriteProgram->UseProgram();
  spriteProgram->setUniformMatrix2fv(spriteProgram_u_viewMatrix_location, viewMatrix);
  
  // Set states for rendering.
  // f->glEnable(GL_MULTISAMPLE);
  // f->glEnable(GL_DEPTH_TEST);
  f->glDisable(GL_CULL_FACE);
  
  // Clear background.
  f->glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  
  CHECK_OPENGL_NO_ERROR();
  
  // Draw the map.
  if (map) {
    map->Render(viewMatrix);
  }
  
  // Draw a sprite.
  if (sprite) {
    QPointF mapCoord = QPointF(0, 0);
    QPointF projectedCoord = map->MapCoordToProjectedCoord(mapCoord);
    
    float framesPerSecond = 30.f;
    int frameIndex = static_cast<int>(framesPerSecond * elapsedSeconds + 0.5f) % sprite->NumFrames();
    Sprite::Frame::Layer& layer = sprite->frame(frameIndex).graphic;
    DrawSprite(projectedCoord.x() - layer.centerX, projectedCoord.y() - layer.centerY, layer.image.width(), layer.image.height(), frameIndex);
  }
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* /*event*/) {
  // TODO
}

void RenderWindow::mouseMoveEvent(QMouseEvent* /*event*/) {
  // TODO
  // TODO: Possibly manually batch these events together, since we disabled event batching globally.
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* /*event*/) {
  // TODO
}

void RenderWindow::wheelEvent(QWheelEvent* event) {
  double degrees = event->angleDelta().y() / 8.0;
  double numSteps = degrees / 15.0;
  
  double scaleFactor = std::pow(std::sqrt(2.0), numSteps);
  zoom *= scaleFactor;
}

void RenderWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Right) {
    scrollRightPressed = true;
    scrollRightPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Left) {
    scrollLeftPressed = true;
    scrollLeftPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Up) {
    scrollUpPressed = true;
    scrollUpPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Down) {
    scrollDownPressed = true;
    scrollDownPressTime = Clock::now();
  }
}

void RenderWindow::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Right) {
    scrollRightPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollRightPressTime).count();
    Scroll(scrollDistancePerSecond / zoom * seconds, 0, &scroll);
  } else if (event->key() == Qt::Key_Left) {
    scrollLeftPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollLeftPressTime).count();
    Scroll(-scrollDistancePerSecond / zoom * seconds, 0, &scroll);
  } else if (event->key() == Qt::Key_Up) {
    scrollUpPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollUpPressTime).count();
    Scroll(0, -scrollDistancePerSecond / zoom * seconds, &scroll);
  } else if (event->key() == Qt::Key_Down) {
    scrollDownPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollDownPressTime).count();
    Scroll(0, scrollDistancePerSecond / zoom * seconds, &scroll);
  }
}
