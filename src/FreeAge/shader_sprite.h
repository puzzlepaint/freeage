#pragma once

#include <memory>

#include <QOpenGLFunctions_3_2_Core>

#include "FreeAge/shader_program.h"

/// Shader for rendering sprites.
class SpriteShader {
 public:
  SpriteShader(bool shadow, bool outline);
  ~SpriteShader();
  
  inline ShaderProgram* GetProgram() { return program.get(); }
  
  inline GLint GetTextureLocation() const { return texture_location; }
  inline GLint GetPlayerColorsTextureLocation() const { return playerColorsTexture_location; }
  inline GLint GetViewMatrixLocation() const { return viewMatrix_location; }
  inline GLint GetSizeLocation() const { return size_location; }
  inline GLint GetTextureSizeLocation() const { return textureSize_location; }
  inline GLint GetPlayerColorsTextureSizeLocation() const { return playerColorsTextureSize_location; }
  inline GLint GetPlayerIndexLocation() const { return playerIndex_location; }
  inline GLint GetTextTopLeftLocation() const { return tex_topleft_location; }
  inline GLint GetTexBottomRightLocation() const { return tex_bottomright_location; }
  inline GLint GetPlayerColorLocation() const { return playerColor_location; }
  
 private:
  std::shared_ptr<ShaderProgram> program;
  GLint texture_location;
  GLint playerColorsTexture_location;
  GLint viewMatrix_location;
  GLint size_location;
  GLint textureSize_location;
  GLint playerColorsTextureSize_location;
  GLint playerIndex_location;
  GLint tex_topleft_location;
  GLint tex_bottomright_location;
  GLint playerColor_location;
};
