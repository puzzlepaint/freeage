#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/client/shader_program.hpp"
#include "FreeAge/client/texture.hpp"

/// Shader for rendering user interface (UI) elements.
class UIShader {
 public:
  UIShader();
  ~UIShader();
  
  inline ShaderProgram* GetProgram() { return program.get(); }
  
  inline GLint GetTextureLocation() const { return texture_location; }
  inline GLint GetViewMatrixLocation() const { return viewMatrix_location; }
  inline GLint GetSizeLocation() const { return size_location; }
  inline GLint GetTexTopLeftLocation() const { return tex_topleft_location; }
  inline GLint GetTexBottomRightLocation() const { return tex_bottomright_location; }
  inline GLint GetModulationColorLocation() const { return modulationColor_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  
  GLint texture_location;
  GLint viewMatrix_location;
  GLint size_location;
  GLint tex_topleft_location;
  GLint tex_bottomright_location;
  GLint modulationColor_location;
};

/// Simple helper function to render a UI element.
void RenderUIGraphic(float x, float y, float width, float height, QRgb modulationColor, GLuint pointBuffer, const Texture& texture, UIShader* uiShader, int widgetWidth, int widgetHeight, QOpenGLFunctions_3_2_Core* f);
