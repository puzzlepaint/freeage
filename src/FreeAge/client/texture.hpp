#pragma once

#include <filesystem>

#include <QImage>
#include <QOpenGLFunctions_3_2_Core>

/// Convenience class to load textures.
class Texture {
 public:
  /// Creates an invalid texture.
  Texture() = default;
  
  /// Frees the texture memory on the GPU.
  ~Texture();
  
  /// Loads the texture from the given QImage into GPU memory. The image can be released afterwards.
  void Load(const QImage& image, int wrapMode, int magFilter, int minFilter);
  
  /// Loads the texture from the given file. Returns true on success, false on failure.
  /// The file is assumed to have 8 bits per color channel, with 4 channels in total.
  bool Load(const std::filesystem::path& path,  int wrapMode, int magFilter, int minFilter);
  
  /// Returns the OpenGL texture Id.
  GLuint GetId() const { return textureId; }
  
  int GetWidth() const { return width; }
  int GetHeight() const { return height; }
  
 private:
  /// OpenGL texture Id.
  GLuint textureId = -1;
  
  /// Width of the texture in pixels.
  int width = -1;
  
  /// Height of the texture in pixels.
  int height;
};
