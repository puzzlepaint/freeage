#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/client/shader_program.hpp"

/// Shader for rendering health bars.
class HealthBarShader {
 public:
  HealthBarShader();
  ~HealthBarShader();
  
  inline ShaderProgram* GetProgram() { return program.get(); }
  
  inline GLint GetViewMatrixLocation() const { return viewMatrix_location; }
  inline GLint GetSizeLocation() const { return size_location; }
  inline GLint GetPlayerColorLocation() const { return playerColor_location; }
  inline GLint GetFillAmountLocation() const { return fillAmount_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  GLint viewMatrix_location;
  GLint size_location;
  GLint playerColor_location;
  GLint fillAmount_location;
};
