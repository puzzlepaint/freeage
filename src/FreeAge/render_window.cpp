#include "FreeAge/render_window.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>

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
}

RenderWindow::~RenderWindow() {
  makeCurrent();
  // Destroy OpenGL resources here
  constantColorProgram.reset();
  doneCurrent();
}

void RenderWindow::SetSprite(Sprite* sprite, const QImage& atlasImage) {
  this->sprite = sprite;
  this->atlasImage = atlasImage;
}

void RenderWindow::CreateConstantColorProgram() {
  constantColorProgram.reset(new ShaderProgram());
  
  CHECK(constantColorProgram->AttachShader(
      "#version 330 core\n"
      "in vec3 in_position;\n"
      "void main() {\n"
      "  gl_Position = vec4(in_position.xyz, 1);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader));
  
  CHECK(constantColorProgram->AttachShader(
      "#version 330 core\n"
      "#extension GL_EXT_geometry_shader : enable\n"
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "\n"
      "uniform float u_width;\n"
      "uniform float u_height;\n"
      "\n"
      "out vec2 texcoord;\n"
      "\n"
      "void main() {\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0, 0);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_width, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0.1, 0);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y - u_height, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0, 0.1);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_width, gl_in[0].gl_Position.y - u_height, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0.1, 0.1);\n"
      "  EmitVertex();\n"
      "  \n"
      "  EndPrimitive();\n"
      "}\n",
      ShaderProgram::ShaderType::kGeometryShader));
  
  CHECK(constantColorProgram->AttachShader(
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
  
  CHECK(constantColorProgram->LinkProgram());
  
  constantColorProgram->UseProgram();
  
  constantColorProgram_u_texture_location =
      constantColorProgram->GetUniformLocationOrAbort("u_texture");
  constantColorProgram_u_width_location =
      constantColorProgram->GetUniformLocationOrAbort("u_width");
  constantColorProgram_u_height_location =
      constantColorProgram->GetUniformLocationOrAbort("u_height");
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
  
  // Load texture
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      atlasImage.width(), atlasImage.height(),
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      atlasImage.scanLine(0));
  
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::DrawTestRect(int x, int y, int width, int height) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  constantColorProgram->UseProgram();
  constantColorProgram->SetUniform1i(constantColorProgram_u_texture_location, 0);  // use GL_TEXTURE0
  constantColorProgram->SetUniform1f(constantColorProgram_u_width_location, width / (0.5f * widgetWidth));
  constantColorProgram->SetUniform1f(constantColorProgram_u_height_location, height / (0.5f * widgetHeight));
  
  // TODO: Set texture coordinates
  // sprite->frame(0).graphic;
  
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  float data[] = {x / (1.f * widgetWidth) * 2.f - 1.f, (1.f - y / (1.f * widgetHeight)) * 2.f - 1.f, 0};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  constantColorProgram->SetPositionAttribute(
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
  
  LoadSprite();
  
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  float data[] = {
      0.f, 0.f, 0};
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::paintGL() {
  LOG(INFO) << "Rendering ...";
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  CHECK_OPENGL_NO_ERROR();
  
  // Set states for rendering.
  // f->glEnable(GL_MULTISAMPLE);
  // f->glEnable(GL_DEPTH_TEST);
  f->glDisable(GL_CULL_FACE);
  
  // Clear background.
  f->glClearColor(0.5f, 0.5f, 0.0f, 1.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  
  CHECK_OPENGL_NO_ERROR();
  
  // DEBUG: Draw a colored shape.
  Sprite::Frame::Layer& layer = sprite->frame(0).graphic;
  DrawTestRect(10, 10, layer.image.width(), layer.image.height());
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
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* /*event*/) {
  // TODO
}

void RenderWindow::wheelEvent(QWheelEvent* /*event*/) {
  // TODO
}

void RenderWindow::keyPressEvent(QKeyEvent* /*event*/) {
  // TODO
}

void RenderWindow::keyReleaseEvent(QKeyEvent* /*event*/) {
  // TODO
}
