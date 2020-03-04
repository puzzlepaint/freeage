#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/client/shader_program.hpp"

/// Shader for rendering the map terrain.
class TerrainShader {
 public:
  TerrainShader();
  ~TerrainShader();
  
  inline ShaderProgram* GetProgram() { return program.get(); }
  
  inline GLint GetViewMatrixLocation() const { return viewMatrix_location; }
  inline GLint GetTextureLocation() const { return texture_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  GLint viewMatrix_location;
  GLint texture_location;
};
