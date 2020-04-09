// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

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
  inline GLint GetViewTextureLocation() const { return viewTexture_location; }
  inline GLint GetTexcoordToMapScalingLocation() const { return texcoordToMapScaling_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  GLint viewMatrix_location;
  GLint texture_location;
  GLint viewTexture_location;
  GLint texcoordToMapScaling_location;
};
