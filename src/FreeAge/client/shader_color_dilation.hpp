// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/client/shader_program.hpp"
#include "FreeAge/client/texture.hpp"

/// Shader for rendering user interface (UI) elements.
class ColorDilationShader {
 public:
  ColorDilationShader();
  ~ColorDilationShader();
  
  inline ShaderProgram* GetProgram() { return program.get(); }
  
  inline GLint GetTextureLocation() const { return texture_location; }
  inline GLint GetPixelStepLocation() const { return pixelStep_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  
  GLint texture_location;
  GLint pixelStep_location;
};
