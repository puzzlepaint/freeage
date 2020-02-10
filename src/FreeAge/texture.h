#pragma once

#include <QImage>
#include <QOpenGLFunctions_3_2_Core>

/// Convenience class to load textures.
class Texture {
 public:
  // Creates an invalid texture.
  Texture();
  
  // Frees the texture memory on the GPU.
  ~Texture();
  
  // Loads the texture from the given QImage into GPU memory. The image can be released afterwards.
  void Load(const QImage& image);
  
  // Returns the OpenGL texture Id.
  GLuint GetId() const { return textureId; }
  
  int GetWidth() const { return width; }
  int GetHeight() const { return height; }
  
 private:
  // OpenGL texture Id.
  GLuint textureId;
  
  /// Width of the texture in pixels.
  int width;
  
  /// Height of the texture in pixels.
  int height;
};
